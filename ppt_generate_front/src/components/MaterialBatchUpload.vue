<template>
  <div class="batch-upload-wrapper">
    <!-- 拖拽/点击上传区域 -->
    <div
      class="drop-zone"
      :class="{ 'drop-zone--active': isDragging, 'drop-zone--disabled': isUploading }"
      @dragover.prevent="isDragging = true"
      @dragleave.prevent="isDragging = false"
      @drop.prevent="handleDrop"
      @click="!isUploading && $refs.fileInput.click()"
    >
      <el-icon class="drop-icon"><Upload /></el-icon>
      <p class="drop-title">拖拽文件到此处，或 <span class="drop-link">点击选择</span></p>
      <p class="drop-hint">支持 PDF、DOCX、TXT，单文件 ≤ {{ maxSizeMb }}MB，最多同时上传 {{ maxFiles }} 个</p>
      <input
        ref="fileInput"
        type="file"
        multiple
        accept=".pdf,.docx,.txt"
        style="display:none"
        @change="handleFileInputChange"
      />
    </div>

    <!-- 文件队列 -->
    <div v-if="queue.length > 0" class="queue-container">
      <div class="queue-header">
        <span class="queue-title">上传队列（{{ queue.length }} 个文件）</span>
        <div class="queue-actions">
          <button
            class="btn-text"
            :disabled="isUploading"
            @click="clearDoneItems"
          >清除已完成</button>
          <button
            class="btn-text danger"
            :disabled="isUploading"
            @click="clearAll"
          >清空队列</button>
        </div>
      </div>

      <transition-group name="list" tag="div" class="queue-list">
        <div
          v-for="item in queue"
          :key="item.uid"
          class="queue-item"
          :class="`queue-item--${item.phase}`"
        >
          <!-- 文件图标 -->
          <div class="qi-icon">
            <el-icon v-if="item.phase === 'done'"><CircleCheck /></el-icon>
            <el-icon v-else-if="item.phase === 'failed'"><CircleClose /></el-icon>
            <el-icon v-else-if="item.phase === 'uploading' || item.phase === 'extracting'">
              <Loading class="spin" />
            </el-icon>
            <el-icon v-else><Document /></el-icon>
          </div>

          <!-- 文件名 + 进度 -->
          <div class="qi-body">
            <div class="qi-name" :title="item.file.name">{{ item.file.name }}</div>
            <div class="qi-meta">
              <span class="qi-size">{{ formatSize(item.file.size) }}</span>
              <span class="qi-status" :class="`status--${item.phase}`">{{ phaseLabel(item) }}</span>
            </div>
            <!-- 上传进度条（仅上传阶段显示） -->
            <div v-if="item.phase === 'uploading'" class="qi-progress-bar">
              <div class="qi-progress-fill" :style="{ width: item.uploadPct + '%' }"></div>
            </div>
            <!-- 提取进度（提取中时显示进度条动画） -->
            <div v-if="item.phase === 'extracting'" class="qi-progress-bar qi-progress-bar--indeterminate">
              <div class="qi-progress-fill qi-progress-fill--slide"></div>
            </div>
            <!-- 错误信息 -->
            <div v-if="item.phase === 'failed' && item.errorMsg" class="qi-error">{{ item.errorMsg }}</div>
          </div>

          <!-- 操作按钮 -->
          <div class="qi-actions">
            <button
              v-if="item.phase === 'done'"
              class="qi-btn primary"
              @click="$emit('use-material', item.material)"
            >用于生成</button>
            <button
              v-if="item.phase === 'pending' && !isUploading"
              class="qi-btn danger"
              @click="removeItem(item.uid)"
            >移除</button>
          </div>
        </div>
      </transition-group>
    </div>

    <!-- 操作栏 -->
    <div v-if="queue.length > 0" class="upload-toolbar">
      <div class="toolbar-summary">
        <span v-if="pendingCount > 0">待上传 {{ pendingCount }} 个</span>
        <span v-if="doneCount > 0" class="success-text">已完成 {{ doneCount }} 个</span>
        <span v-if="failedCount > 0" class="danger-text">失败 {{ failedCount }} 个</span>
      </div>
      <button
        class="btn-primary"
        :disabled="isUploading || pendingCount === 0"
        @click="startUpload"
      >
        <el-icon v-if="isUploading" class="spin"><Loading /></el-icon>
        <el-icon v-else><Upload /></el-icon>
        {{ isUploading ? `上传中… ${overallPct}%` : `开始上传 (${pendingCount} 个)` }}
      </button>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onBeforeUnmount } from 'vue'
import { ElMessage } from 'element-plus'
import {
  Upload, Document, CircleCheck, CircleClose, Loading
} from '@element-plus/icons-vue'
import materialAPI from '@/api/material.js'

/**
 * 并发限制执行器
 * 将 tasks（均为返回 Promise 的函数）以最多 concurrency 个并行方式执行完毕。
 * @param {Array<() => Promise>} tasks
 * @param {number} concurrency
 */
async function runWithConcurrency(tasks, concurrency) {
  const queue = [...tasks]
  const workers = Array.from({ length: Math.min(concurrency, queue.length) }, async () => {
    while (queue.length > 0) {
      const task = queue.shift()
      if (task) await task().catch(() => {/* 单任务失败不影响其他任务 */})
    }
  })
  await Promise.all(workers)
}

// ---- props ----
const props = defineProps({
  maxFiles:  { type: Number, default: 10 },
  maxSizeMb: { type: Number, default: 20 },
  /** 并发提取轮询不占用上传队列，p-queue 仅控制上传并发 */
  concurrency: { type: Number, default: 3 },
})

const emit = defineEmits(['uploaded', 'use-material', 'queue-change'])

// ---- 文件队列条目结构 ----
// phase: 'pending' | 'uploading' | 'extracting' | 'done' | 'failed'
let uidCounter = 0
const makeItem = (file) => ({
  uid:        ++uidCounter,
  file,
  phase:      'pending',
  uploadPct:  0,
  material:   null,   // 上传成功后后端返回的 material 对象
  errorMsg:   '',
  pollTimer:  null,
})

// ---- 响应式状态 ----
const queue      = ref([])
const isDragging = ref(false)
const isUploading = ref(false)
const overallPct  = ref(0)

// ---- computed ----
const pendingCount  = computed(() => queue.value.filter(i => i.phase === 'pending').length)
const doneCount     = computed(() => queue.value.filter(i => i.phase === 'done').length)
const failedCount   = computed(() => queue.value.filter(i => i.phase === 'failed').length)

// ---- 工具函数 ----
const formatSize = (bytes) => {
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
  return (bytes / 1024 / 1024).toFixed(1) + ' MB'
}

const phaseLabel = (item) => {
  const map = {
    pending:    '待上传',
    uploading:  `上传中 ${item.uploadPct}%`,
    extracting: '提取中…',
    done:       '提取完成',
    failed:     '失败',
  }
  return map[item.phase] || item.phase
}

// ---- 文件校验 ----
const ALLOWED_EXT = ['pdf', 'docx', 'txt']

const validateFile = (file) => {
  const ext = file.name.split('.').pop()?.toLowerCase()
  if (!ALLOWED_EXT.includes(ext)) {
    return `${file.name}：不支持的文件类型（仅限 PDF、DOCX、TXT）`
  }
  if (file.size > props.maxSizeMb * 1024 * 1024) {
    return `${file.name}：文件超过 ${props.maxSizeMb}MB 限制`
  }
  if (file.size === 0) {
    return `${file.name}：文件为空`
  }
  return null
}

// ---- 添加文件到队列 ----
const addFiles = (files) => {
  const fileArr = Array.from(files)
  const available = props.maxFiles - queue.value.length
  if (available <= 0) {
    ElMessage.warning(`队列已满，最多 ${props.maxFiles} 个文件`)
    return
  }

  let addedCount = 0
  for (const file of fileArr.slice(0, available)) {
    const err = validateFile(file)
    if (err) {
      ElMessage.warning(err)
      continue
    }
    // 去重（同名同大小）
    const dup = queue.value.find(i => i.file.name === file.name && i.file.size === file.size)
    if (dup) {
      ElMessage.warning(`${file.name} 已在队列中`)
      continue
    }
    queue.value.push(makeItem(file))
    addedCount++
  }
  if (addedCount > 0) {
    emit('queue-change', queue.value.length)
  }
}

const handleFileInputChange = (e) => {
  addFiles(e.target.files)
  e.target.value = ''  // 允许重复选同一文件
}

const handleDrop = (e) => {
  isDragging.value = false
  addFiles(e.dataTransfer.files)
}

// ---- 队列管理 ----
const removeItem = (uid) => {
  queue.value = queue.value.filter(i => i.uid !== uid)
  emit('queue-change', queue.value.length)
}

const clearDoneItems = () => {
  queue.value.filter(i => i.phase === 'done').forEach(i => stopPoll(i))
  queue.value = queue.value.filter(i => i.phase !== 'done')
  emit('queue-change', queue.value.length)
}

const clearAll = () => {
  queue.value.forEach(i => stopPoll(i))
  queue.value = []
  emit('queue-change', 0)
}

// ---- 提取状态轮询 ----
const stopPoll = (item) => {
  if (item.pollTimer) {
    clearInterval(item.pollTimer)
    item.pollTimer = null
  }
}

const startPoll = (item) => {
  stopPoll(item)
  item.pollTimer = setInterval(async () => {
    try {
      const res = await materialAPI.getStatus(item.material.id)
      const mat = res.data?.material
      if (!mat) return
      if (mat.status === 'completed') {
        stopPoll(item)
        item.phase = 'done'
        // 拉取完整结果以便"用于生成"时有 extractResult
        try {
          const resultRes = await materialAPI.getResult(item.material.id)
          item.material = resultRes.data?.material || item.material
        } catch (_) { /* 已完成状态即可，结果拉取失败不影响标记 */ }
        emit('uploaded', item.material)
      } else if (mat.status === 'failed') {
        stopPoll(item)
        item.phase    = 'failed'
        item.errorMsg = mat.errorMsg || '提取失败'
      }
    } catch (e) {
      console.error('轮询失败', e)
    }
  }, 2500)
}

// ---- 单文件上传任务（投入 p-queue） ----
const uploadOne = async (item) => {
  item.phase     = 'uploading'
  item.uploadPct = 0

  try {
    const formData = new FormData()
    formData.append('files[]', item.file)

    const res = await materialAPI.batchUpload([item.file], (pct) => {
      item.uploadPct = pct
    })

    const results = res.data?.results || []
    const first   = results[0]

    if (!first?.success) {
      item.phase    = 'failed'
      item.errorMsg = first?.message || '上传失败'
      return
    }

    item.material  = first.material
    item.phase     = 'extracting'
    startPoll(item)
  } catch (e) {
    item.phase    = 'failed'
    item.errorMsg = e?.response?.data?.message || e?.message || '网络错误'
  }
}

// ---- 开始批量上传 ----
const startUpload = async () => {
  const pending = queue.value.filter(i => i.phase === 'pending')
  if (pending.length === 0) return

  isUploading.value = true
  overallPct.value  = 0

  let completedTasks = 0
  const total = pending.length

  // 每个文件包装为一个任务函数，由 runWithConcurrency 控制并发数
  const tasks = pending.map(item => async () => {
    await uploadOne(item)
    completedTasks++
    overallPct.value = Math.round((completedTasks / total) * 100)
  })

  await runWithConcurrency(tasks, props.concurrency)

  isUploading.value = false

  const succeededCount = pending.filter(i => i.phase === 'extracting' || i.phase === 'done').length
  const failedCount    = pending.filter(i => i.phase === 'failed').length

  if (succeededCount > 0) {
    ElMessage.success(`${succeededCount} 个文件上传成功，正在后台提取关键信息…`)
  }
  if (failedCount > 0) {
    ElMessage.error(`${failedCount} 个文件上传失败，请检查队列中的错误信息`)
  }
}

// ---- 清理 ----
onBeforeUnmount(() => {
  queue.value.forEach(i => stopPoll(i))
})
</script>

<style scoped>
/* ---- 拖拽区域 ---- */
.drop-zone {
  border: 2px dashed #cbd5e1;
  border-radius: 12px;
  padding: 2rem 1.5rem;
  text-align: center;
  cursor: pointer;
  transition: border-color 0.2s, background 0.2s;
  background: #f8fafc;
}
.drop-zone:hover:not(.drop-zone--disabled) {
  border-color: #6366f1;
  background: #eef2ff;
}
.drop-zone--active {
  border-color: #6366f1;
  background: #eef2ff;
}
.drop-zone--disabled {
  cursor: not-allowed;
  opacity: 0.6;
}
.drop-icon {
  font-size: 2.5rem;
  color: #94a3b8;
  margin-bottom: 0.5rem;
}
.drop-title {
  font-size: 0.95rem;
  color: #475569;
  margin: 0.25rem 0;
}
.drop-link {
  color: #6366f1;
  font-weight: 600;
}
.drop-hint {
  font-size: 0.78rem;
  color: #94a3b8;
  margin: 0;
}

/* ---- 队列容器 ---- */
.queue-container {
  margin-top: 1rem;
  border: 1px solid #e2e8f0;
  border-radius: 10px;
  overflow: hidden;
}
.queue-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.65rem 1rem;
  background: #f1f5f9;
  border-bottom: 1px solid #e2e8f0;
}
.queue-title {
  font-size: 0.85rem;
  font-weight: 600;
  color: #475569;
}
.queue-actions {
  display: flex;
  gap: 0.75rem;
}

.queue-list {
  max-height: 360px;
  overflow-y: auto;
}

/* ---- 队列条目 ---- */
.queue-item {
  display: flex;
  align-items: flex-start;
  gap: 0.75rem;
  padding: 0.75rem 1rem;
  border-bottom: 1px solid #f1f5f9;
  transition: background 0.15s;
}
.queue-item:last-child { border-bottom: none; }
.queue-item--done     { background: #f0fdf4; }
.queue-item--failed   { background: #fff5f5; }
.queue-item--uploading,
.queue-item--extracting { background: #f8f9ff; }

.qi-icon {
  flex-shrink: 0;
  font-size: 1.3rem;
  padding-top: 2px;
  color: #94a3b8;
}
.queue-item--done    .qi-icon { color: #22c55e; }
.queue-item--failed  .qi-icon { color: #ef4444; }
.queue-item--uploading .qi-icon,
.queue-item--extracting .qi-icon { color: #6366f1; }

.qi-body {
  flex: 1;
  min-width: 0;
}
.qi-name {
  font-size: 0.875rem;
  font-weight: 500;
  color: #1e293b;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.qi-meta {
  display: flex;
  gap: 0.75rem;
  margin-top: 2px;
  font-size: 0.75rem;
  color: #94a3b8;
}
.qi-status { font-weight: 500; }
.status--done      { color: #22c55e; }
.status--failed    { color: #ef4444; }
.status--uploading,
.status--extracting { color: #6366f1; }

/* 进度条 */
.qi-progress-bar {
  height: 4px;
  background: #e2e8f0;
  border-radius: 4px;
  margin-top: 6px;
  overflow: hidden;
}
.qi-progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #6366f1, #818cf8);
  border-radius: 4px;
  transition: width 0.3s ease;
}
.qi-progress-bar--indeterminate .qi-progress-fill--slide {
  width: 40%;
  animation: slide 1.4s ease-in-out infinite;
}
@keyframes slide {
  0%   { transform: translateX(-200%); }
  100% { transform: translateX(350%); }
}

.qi-error {
  margin-top: 4px;
  font-size: 0.72rem;
  color: #ef4444;
}

.qi-actions {
  flex-shrink: 0;
  display: flex;
  gap: 0.4rem;
  align-items: center;
}
.qi-btn {
  font-size: 0.75rem;
  padding: 3px 10px;
  border-radius: 6px;
  border: none;
  cursor: pointer;
  font-weight: 500;
  transition: opacity 0.15s;
}
.qi-btn:hover { opacity: 0.8; }
.qi-btn.primary { background: #6366f1; color: #fff; }
.qi-btn.danger  { background: #fee2e2; color: #ef4444; }

/* ---- 操作栏 ---- */
.upload-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: 1rem;
  gap: 1rem;
}
.toolbar-summary {
  display: flex;
  gap: 0.75rem;
  font-size: 0.82rem;
  color: #64748b;
}
.success-text { color: #22c55e; font-weight: 600; }
.danger-text  { color: #ef4444; font-weight: 600; }

.btn-primary {
  display: inline-flex;
  align-items: center;
  gap: 0.4rem;
  padding: 0.5rem 1.25rem;
  background: linear-gradient(135deg, #6366f1, #818cf8);
  color: #fff;
  border: none;
  border-radius: 8px;
  font-size: 0.875rem;
  font-weight: 600;
  cursor: pointer;
  transition: opacity 0.2s, transform 0.1s;
}
.btn-primary:hover:not(:disabled) { opacity: 0.9; transform: translateY(-1px); }
.btn-primary:disabled { opacity: 0.5; cursor: not-allowed; }

.btn-text {
  background: none;
  border: none;
  font-size: 0.78rem;
  color: #64748b;
  cursor: pointer;
  padding: 2px 6px;
  border-radius: 4px;
  transition: background 0.15s;
}
.btn-text:hover:not(:disabled) { background: #f1f5f9; }
.btn-text.danger  { color: #ef4444; }
.btn-text:disabled { opacity: 0.4; cursor: not-allowed; }

/* ---- 列表动画 ---- */
.list-enter-active,
.list-leave-active { transition: all 0.25s ease; }
.list-enter-from   { opacity: 0; transform: translateY(-8px); }
.list-leave-to     { opacity: 0; transform: translateX(16px); }

/* ---- 旋转动画 ---- */
.spin { animation: spin 1s linear infinite; }
@keyframes spin {
  from { transform: rotate(0deg); }
  to   { transform: rotate(360deg); }
}
</style>
