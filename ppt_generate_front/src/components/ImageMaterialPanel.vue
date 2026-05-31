<template>
  <div class="image-material-panel">
    <div class="img-panel-head">
      <div class="img-panel-title-group">
        <div class="img-panel-badge">Image Library</div>
        <h3 class="img-panel-title">{{ selectable ? '选择图片素材' : '图片素材库' }}</h3>
        <p class="img-panel-subtitle">
          {{ selectable ? '选择后的图片会优先用于生成页配图。' : '支持批量上传、批量删除，并展示更清晰的图片摘要与状态。' }}
        </p>
      </div>
      <div class="img-panel-metrics">
        <div class="img-metric-chip">
          <strong>{{ images.length }}</strong>
          <span>总图片</span>
        </div>
        <div class="img-metric-chip">
          <strong>{{ readyCount }}</strong>
          <span>可用</span>
        </div>
        <div v-if="!selectable" class="img-metric-chip img-metric-chip--accent">
          <strong>{{ batchMode ? managedSelectedIds.size : 0 }}</strong>
          <span>已选中</span>
        </div>
      </div>
    </div>

    <div
      class="img-upload-zone"
      :class="{ 'img-upload-zone--drag': isDragging, 'img-upload-zone--disabled': uploading }"
      @dragover.prevent="isDragging = true"
      @dragleave.prevent="isDragging = false"
      @drop.prevent="onDrop"
      @click="!uploading && $refs.imgFileInput.click()"
    >
      <input
        ref="imgFileInput"
        type="file"
        accept="image/jpeg,image/png,image/webp,image/gif"
        multiple
        style="display:none"
        @change="onFileChange"
      />
      <div class="upload-zone-inner">
        <div class="upload-visual">
          <span class="upload-icon">🖼️</span>
          <span class="upload-pulse"></span>
        </div>
        <div class="upload-copy">
          <p class="upload-title">拖拽图片到这里，或点击批量上传</p>
          <p class="upload-hint">支持 `JPG / PNG / WebP / GIF`，单张最大 20MB，单次最多 20 张</p>
        </div>
        <div class="upload-actions">
          <span v-if="uploading" class="upload-progress">{{ uploadProgress }}%</span>
          <span v-else class="upload-action-chip">{{ selectable ? '补充图片素材' : '批量上传' }}</span>
        </div>
      </div>
      <div v-if="uploading" class="upload-progress-bar">
        <div class="upload-progress-fill" :style="{ width: `${uploadProgress}%` }"></div>
      </div>
    </div>

    <div class="img-toolbar" v-if="images.length || !selectable">
      <div class="img-toolbar-left">
        <button
          v-if="!selectable"
          class="img-toolbar-btn"
          :class="{ 'img-toolbar-btn--active': batchMode }"
          @click="toggleBatchMode"
        >
          {{ batchMode ? '退出批量管理' : '批量管理' }}
        </button>
        <template v-if="!selectable && batchMode">
          <label class="img-select-all">
            <input
              type="checkbox"
              :checked="allManagedSelected"
              :indeterminate.prop="managedIndeterminate"
              @change="toggleSelectAll"
            />
            <span>全选</span>
          </label>
          <span class="img-selected-count">已选 {{ managedSelectedIds.size }} 项</span>
        </template>
      </div>
      <div class="img-toolbar-right">
        <button
          v-if="!selectable && batchMode"
          class="img-toolbar-btn img-toolbar-btn--danger"
          :disabled="managedSelectedIds.size === 0"
          @click="batchDeleteSelected"
        >
          批量删除
        </button>
        <button class="img-toolbar-btn" :disabled="loading" @click="loadImages">
          {{ loading ? '刷新中…' : '刷新列表' }}
        </button>
      </div>
    </div>

    <div v-if="loading" class="img-loading-state">
      <div class="img-loading-orb"></div>
      <p>正在同步图片素材…</p>
    </div>

    <div v-else-if="images.length" class="img-grid">
      <article
        v-for="img in images"
        :key="img.id"
        class="img-card"
        :class="{
          'img-card--selected': isSelected(img.id),
          'img-card--selectable': selectable || batchMode
        }"
        @click="handleCardClick(img.id)"
      >
        <div class="img-preview-wrap">
          <img
            v-if="thumbUrls[img.id]"
            :src="thumbUrls[img.id]"
            class="img-thumb"
            :alt="img.originalFilename || img.original_filename || img.filename"
            loading="lazy"
          />
          <div v-else class="img-placeholder">
            <span v-if="img.status === 'pending' || img.status === 'indexing'" class="status-spin">⟳</span>
            <span v-else>🖼️</span>
          </div>
          <div class="img-preview-overlay"></div>
          <div v-if="selectable || batchMode" class="img-card-check" :class="{ 'img-card-check--active': isSelected(img.id) }">
            {{ isSelected(img.id) ? '✓' : '' }}
          </div>
          <span class="img-status-badge" :class="'img-status-badge--' + img.status">
            {{ statusLabel(img.status) }}
          </span>
        </div>

        <div class="img-card-info">
          <div class="img-card-headline">
            <div class="img-name" :title="img.originalFilename || img.original_filename || img.filename">
              {{ img.originalFilename || img.original_filename || img.filename }}
            </div>
            <span class="img-file-size">{{ formatFileSize(img.fileSize || img.file_size) }}</span>
          </div>
          <div class="img-desc" :title="img.description || ''">
            {{ img.description || '暂未生成图片摘要，系统会在分析完成后自动补充。' }}
          </div>
          <div class="img-meta">
            <span class="img-meta-item">ID: {{ shortId(img.id) }}</span>
            <span class="img-meta-dot">·</span>
            <span class="img-meta-item">{{ formatTime(img.createdAt || img.created_at) }}</span>
          </div>
          <div class="img-card-actions">
            <button
              v-if="!selectable"
              class="img-action-btn"
              @click.stop="copyImageId(img.id)"
            >
              复制 ID
            </button>
            <button
              v-if="!selectable"
              class="img-action-btn img-action-btn--delete"
              @click.stop="deleteOne(img)"
            >
              删除
            </button>
            <span v-else class="img-select-hint">{{ isSelected(img.id) ? '已加入配图候选' : '点击加入配图候选' }}</span>
          </div>
        </div>
      </article>
    </div>

    <div v-else class="img-empty">
      <div class="img-empty-icon">🖼️</div>
      <h4>还没有图片素材</h4>
      <p>上传后可在 PPT 生成中优先使用，也可以在这里统一管理。</p>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import dayjs from 'dayjs'
import { imageMaterialApi } from '@/api/material'

const props = defineProps({
  selectable: { type: Boolean, default: false },
  modelValue: { type: Array, default: () => [] }
})

const emit = defineEmits(['update:modelValue'])

const images = ref([])
const loading = ref(false)
const uploading = ref(false)
const uploadProgress = ref(0)
const isDragging = ref(false)
const thumbUrls = ref({})
const selectedIds = ref(new Set(props.modelValue))
const managedSelectedIds = ref(new Set())
const batchMode = ref(false)

const readyCount = computed(() => images.value.filter(img => img.status === 'ready').length)
const allManagedSelected = computed(() =>
  images.value.length > 0 && images.value.every(img => managedSelectedIds.value.has(img.id))
)
const managedIndeterminate = computed(() =>
  managedSelectedIds.value.size > 0 && !allManagedSelected.value
)

watch(() => props.modelValue, (value) => {
  selectedIds.value = new Set(value || [])
})

function isSelected(id) {
  return props.selectable ? selectedIds.value.has(id) : managedSelectedIds.value.has(id)
}

function syncThumbs(list) {
  for (const img of list) {
    if (!thumbUrls.value[img.id]) {
      thumbUrls.value[img.id] = imageMaterialApi.fileUrl(img.id)
    }
  }
}

async function loadImages() {
  loading.value = true
  try {
    const res = await imageMaterialApi.list()
    images.value = res.data?.images || []
    syncThumbs(images.value)
    if (!props.selectable && batchMode.value) {
      const next = new Set()
      images.value.forEach((img) => {
        if (managedSelectedIds.value.has(img.id)) next.add(img.id)
      })
      managedSelectedIds.value = next
    }
  } catch (e) {
    console.error('ImageMaterialPanel: load failed', e)
    ElMessage.error('加载图片素材失败')
  } finally {
    loading.value = false
  }
}

function validateFiles(files) {
  const allowed = ['image/jpeg', 'image/png', 'image/webp', 'image/gif']
  const valid = []
  const errors = []
  Array.from(files || []).forEach((file) => {
    if (!allowed.includes(file.type)) {
      errors.push(`${file.name} 格式不支持`)
      return
    }
    if (file.size > 20 * 1024 * 1024) {
      errors.push(`${file.name} 超过 20MB`)
      return
    }
    valid.push(file)
  })
  if (valid.length > 20) {
    errors.push('单次最多上传 20 张图片')
    valid.splice(20)
  }
  return { valid, errors }
}

async function uploadFiles(files) {
  const { valid, errors } = validateFiles(files)
  if (errors.length) {
    ElMessage.warning(errors.slice(0, 3).join('；'))
  }
  if (valid.length === 0) return

  uploading.value = true
  uploadProgress.value = 0
  try {
    if (valid.length === 1) {
      const res = await imageMaterialApi.upload(valid[0], (p) => { uploadProgress.value = p })
      const img = res.data?.image
      if (img) {
        images.value.unshift(img)
        syncThumbs([img])
        pollStatus(img.id)
        ElMessage.success('图片已上传，正在分析内容')
      }
      return
    }

    const res = await imageMaterialApi.batchUpload(valid, (p) => { uploadProgress.value = p })
    const results = Array.isArray(res.data?.results) ? res.data.results : []
    const created = results
      .filter(item => item.success && item.image)
      .map(item => item.image)

    if (created.length) {
      images.value = [...created, ...images.value]
      syncThumbs(created)
      created.forEach((img) => pollStatus(img.id))
    }

    const succeeded = Number(res.data?.succeeded || created.length || 0)
    const failed = Number(res.data?.failed || Math.max(valid.length - created.length, 0))
    if (failed > 0) {
      ElMessage.warning(`批量上传完成：成功 ${succeeded} 张，失败 ${failed} 张`)
    } else {
      ElMessage.success(`批量上传成功，共 ${succeeded} 张`)
    }
  } catch (e) {
    console.error('ImageMaterialPanel: upload failed', e)
    ElMessage.error('上传失败：' + (e?.response?.data?.message || e.message || '未知错误'))
  } finally {
    uploading.value = false
    uploadProgress.value = 0
  }
}

function pollStatus(id, maxAttempts = 30, interval = 3000) {
  let attempts = 0
  const timer = setInterval(async () => {
    attempts += 1
    try {
      const res = await imageMaterialApi.getStatus(id)
      const img = res.data?.image
      if (!img) {
        clearInterval(timer)
        return
      }
      const idx = images.value.findIndex(item => item.id === id)
      if (idx >= 0) {
        images.value[idx] = img
        syncThumbs([img])
        if (img.status === 'ready' || img.status === 'failed') {
          clearInterval(timer)
        }
      } else {
        clearInterval(timer)
      }
    } catch (_) {
      // ignore
    }
    if (attempts >= maxAttempts) clearInterval(timer)
  }, interval)
}

function onFileChange(e) {
  uploadFiles(e.target.files)
  e.target.value = ''
}

function onDrop(e) {
  isDragging.value = false
  uploadFiles(e.dataTransfer.files)
}

function handleCardClick(id) {
  if (props.selectable) {
    const next = new Set(selectedIds.value)
    if (next.has(id)) next.delete(id)
    else next.add(id)
    selectedIds.value = next
    emit('update:modelValue', [...next])
    return
  }
  if (batchMode.value) {
    const next = new Set(managedSelectedIds.value)
    if (next.has(id)) next.delete(id)
    else next.add(id)
    managedSelectedIds.value = next
  }
}

function toggleBatchMode() {
  batchMode.value = !batchMode.value
  if (!batchMode.value) {
    managedSelectedIds.value = new Set()
  }
}

function toggleSelectAll(e) {
  if (!e.target.checked) {
    managedSelectedIds.value = new Set()
    return
  }
  managedSelectedIds.value = new Set(images.value.map(img => img.id))
}

async function deleteOne(img) {
  try {
    await ElMessageBox.confirm(
      `确定删除「${img.originalFilename || img.original_filename || img.filename}」吗？此操作不可恢复。`,
      '删除图片素材',
      { confirmButtonText: '删除', cancelButtonText: '取消', type: 'warning' }
    )
    await imageMaterialApi.remove(img.id)
    removeImagesByIds([img.id])
    ElMessage.success('删除成功')
  } catch (e) {
    if (e === 'cancel' || e === 'close' || e?.message === 'cancel') return
    ElMessage.error('删除失败：' + (e?.response?.data?.message || e.message || '未知错误'))
  }
}

async function batchDeleteSelected() {
  const ids = [...managedSelectedIds.value]
  if (ids.length === 0) return
  try {
    await ElMessageBox.confirm(
      `确定批量删除已选中的 ${ids.length} 张图片吗？此操作不可恢复。`,
      '批量删除图片素材',
      { confirmButtonText: '批量删除', cancelButtonText: '取消', type: 'warning' }
    )
    const res = await imageMaterialApi.batchDelete(ids)
    const results = Array.isArray(res.data?.results) ? res.data.results : []
    const successIds = results.filter(item => item.status === 'ok').map(item => item.id)
    if (successIds.length) {
      removeImagesByIds(successIds)
    }
    const failed = Number(res.data?.failed || Math.max(ids.length - successIds.length, 0))
    if (failed > 0) {
      ElMessage.warning(`已删除 ${successIds.length} 张，${failed} 张删除失败`)
    } else {
      ElMessage.success(`已删除 ${successIds.length} 张图片`)
    }
    managedSelectedIds.value = new Set()
    batchMode.value = false
  } catch (e) {
    if (e === 'cancel' || e === 'close' || e?.message === 'cancel') return
    ElMessage.error('批量删除失败：' + (e?.response?.data?.message || e.message || '未知错误'))
  }
}

function removeImagesByIds(ids) {
  const idSet = new Set(ids)
  images.value = images.value.filter(item => !idSet.has(item.id))
  ids.forEach((id) => {
    delete thumbUrls.value[id]
  })
  if (props.selectable) {
    const next = new Set(selectedIds.value)
    ids.forEach(id => next.delete(id))
    selectedIds.value = next
    emit('update:modelValue', [...next])
  }
  const nextManaged = new Set(managedSelectedIds.value)
  ids.forEach(id => nextManaged.delete(id))
  managedSelectedIds.value = nextManaged
}

async function copyImageId(id) {
  try {
    await navigator.clipboard.writeText(String(id))
    ElMessage.success('图片 ID 已复制')
  } catch (_) {
    ElMessage.warning('复制失败，请手动复制')
  }
}

function formatFileSize(bytes) {
  if (!bytes) return '0 B'
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`
}

function formatTime(value) {
  if (!value) return '刚刚上传'
  const d = dayjs(typeof value === 'number' ? value * 1000 : value)
  return d.isValid() ? d.format('MM/DD HH:mm') : '刚刚上传'
}

function shortId(id) {
  const raw = String(id || '')
  return raw.length > 8 ? raw.slice(0, 8) : raw
}

function statusLabel(status) {
  const map = {
    pending: '等待分析',
    indexing: '分析中',
    ready: '就绪',
    failed: '失败',
  }
  return map[status] || status || '未知'
}

onMounted(loadImages)

defineExpose({ reload: loadImages })
</script>

<style scoped>
.image-material-panel {
  width: 100%;
  display: flex;
  flex-direction: column;
  gap: 18px;
}

.img-panel-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
}

.img-panel-title-group {
  min-width: 0;
}

.img-panel-badge {
  display: inline-flex;
  align-items: center;
  padding: 4px 10px;
  border-radius: 999px;
  background: rgba(14, 165, 233, 0.1);
  color: #0369a1;
  font-size: 11px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  margin-bottom: 10px;
}

.img-panel-title {
  margin: 0;
  font-size: 20px;
  color: #0f172a;
  line-height: 1.2;
}

.img-panel-subtitle {
  margin: 8px 0 0;
  font-size: 13px;
  color: #64748b;
  line-height: 1.7;
  max-width: 620px;
}

.img-panel-metrics {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
  justify-content: flex-end;
}

.img-metric-chip {
  min-width: 82px;
  padding: 12px 14px;
  border-radius: 16px;
  background: linear-gradient(180deg, #ffffff, #f8fafc);
  border: 1px solid rgba(148, 163, 184, 0.22);
  box-shadow: 0 10px 24px rgba(15, 23, 42, 0.05);
  display: flex;
  flex-direction: column;
  gap: 3px;
}

.img-metric-chip strong {
  font-size: 20px;
  color: #0f172a;
  line-height: 1;
}

.img-metric-chip span {
  font-size: 12px;
  color: #64748b;
}

.img-metric-chip--accent {
  background: linear-gradient(180deg, #eff6ff, #dbeafe);
}

.img-upload-zone {
  position: relative;
  overflow: hidden;
  border-radius: 22px;
  border: 1px dashed rgba(14, 165, 233, 0.35);
  background:
    radial-gradient(circle at top left, rgba(56, 189, 248, 0.12), transparent 30%),
    linear-gradient(180deg, #ffffff, #f8fbff);
  padding: 22px 24px;
  cursor: pointer;
  transition: border-color 0.2s ease, transform 0.2s ease, box-shadow 0.2s ease;
}

.img-upload-zone:hover,
.img-upload-zone--drag {
  border-color: rgba(14, 165, 233, 0.6);
  transform: translateY(-1px);
  box-shadow: 0 20px 40px rgba(14, 165, 233, 0.12);
}

.img-upload-zone--disabled {
  cursor: not-allowed;
  opacity: 0.72;
}

.upload-zone-inner {
  pointer-events: none;
  display: flex;
  gap: 18px;
  align-items: center;
  justify-content: space-between;
}

.upload-visual {
  position: relative;
  width: 64px;
  height: 64px;
  border-radius: 20px;
  background: linear-gradient(135deg, #0ea5e9, #2563eb);
  color: #fff;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 30px;
  flex-shrink: 0;
  box-shadow: 0 12px 30px rgba(37, 99, 235, 0.25);
}

.upload-pulse {
  position: absolute;
  inset: -6px;
  border-radius: 24px;
  border: 1px solid rgba(37, 99, 235, 0.18);
}

.upload-copy {
  min-width: 0;
  flex: 1;
}

.upload-title {
  margin: 0 0 6px;
  font-size: 16px;
  color: #0f172a;
  font-weight: 600;
}

.upload-hint {
  margin: 0;
  font-size: 13px;
  color: #64748b;
}

.upload-actions {
  flex-shrink: 0;
}

.upload-action-chip,
.upload-progress {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 8px 14px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
}

.upload-action-chip {
  background: rgba(14, 165, 233, 0.1);
  color: #0284c7;
}

.upload-progress {
  background: rgba(37, 99, 235, 0.12);
  color: #1d4ed8;
  min-width: 70px;
}

.upload-progress-bar {
  height: 5px;
  margin-top: 16px;
  border-radius: 999px;
  background: rgba(148, 163, 184, 0.18);
  overflow: hidden;
}

.upload-progress-fill {
  height: 100%;
  border-radius: 999px;
  background: linear-gradient(90deg, #0ea5e9, #2563eb);
  transition: width 0.2s ease;
}

.img-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 14px;
  flex-wrap: wrap;
}

.img-toolbar-left,
.img-toolbar-right {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.img-toolbar-btn {
  padding: 9px 14px;
  border-radius: 12px;
  border: 1px solid rgba(148, 163, 184, 0.25);
  background: #fff;
  color: #0f172a;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.18s ease;
}

.img-toolbar-btn:hover:not(:disabled) {
  border-color: rgba(14, 165, 233, 0.35);
  color: #0369a1;
  background: #f8fbff;
}

.img-toolbar-btn:disabled {
  cursor: not-allowed;
  opacity: 0.45;
}

.img-toolbar-btn--active {
  background: linear-gradient(135deg, #0ea5e9, #2563eb);
  color: #fff;
  border-color: transparent;
}

.img-toolbar-btn--danger {
  background: #fff1f2;
  color: #be123c;
  border-color: rgba(244, 63, 94, 0.18);
}

.img-select-all {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  color: #334155;
  font-size: 13px;
}

.img-selected-count {
  font-size: 13px;
  color: #0369a1;
  font-weight: 600;
}

.img-loading-state,
.img-empty {
  border-radius: 20px;
  border: 1px solid rgba(148, 163, 184, 0.16);
  background: linear-gradient(180deg, #ffffff, #f8fafc);
  padding: 42px 20px;
  text-align: center;
}

.img-loading-state p,
.img-empty p {
  margin: 0;
  color: #64748b;
  font-size: 13px;
}

.img-loading-orb,
.img-empty-icon {
  width: 54px;
  height: 54px;
  margin: 0 auto 14px;
  border-radius: 18px;
}

.img-loading-orb {
  background: linear-gradient(135deg, #0ea5e9, #2563eb);
  animation: imgPulse 1.2s ease-in-out infinite alternate;
}

.img-empty-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #e0f2fe, #dbeafe);
  font-size: 28px;
}

.img-empty h4 {
  margin: 0 0 8px;
  color: #0f172a;
}

.img-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(230px, 1fr));
  gap: 16px;
}

.img-card {
  overflow: hidden;
  border-radius: 22px;
  border: 1px solid rgba(148, 163, 184, 0.18);
  background: linear-gradient(180deg, #ffffff, #fbfdff);
  box-shadow: 0 18px 36px rgba(15, 23, 42, 0.06);
  transition: transform 0.18s ease, box-shadow 0.18s ease, border-color 0.18s ease;
}

.img-card--selectable {
  cursor: pointer;
}

.img-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 24px 44px rgba(15, 23, 42, 0.1);
  border-color: rgba(14, 165, 233, 0.24);
}

.img-card--selected {
  border-color: rgba(37, 99, 235, 0.46);
  box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.16), 0 24px 44px rgba(37, 99, 235, 0.12);
}

.img-preview-wrap {
  position: relative;
  aspect-ratio: 4 / 3;
  background: #eef2f7;
}

.img-thumb,
.img-placeholder,
.img-preview-overlay {
  position: absolute;
  inset: 0;
}

.img-thumb {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.img-preview-overlay {
  background: linear-gradient(180deg, transparent 55%, rgba(15, 23, 42, 0.5) 100%);
  pointer-events: none;
}

.img-placeholder {
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 34px;
  color: #cbd5e1;
}

.status-spin {
  display: inline-block;
  animation: spin 1.4s linear infinite;
}

.img-card-check {
  position: absolute;
  top: 12px;
  right: 12px;
  width: 26px;
  height: 26px;
  border-radius: 999px;
  border: 1px solid rgba(255, 255, 255, 0.72);
  background: rgba(255, 255, 255, 0.22);
  color: #fff;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 13px;
  font-weight: 700;
  backdrop-filter: blur(6px);
}

.img-card-check--active {
  background: #2563eb;
  border-color: #2563eb;
}

.img-status-badge {
  position: absolute;
  left: 12px;
  bottom: 12px;
  padding: 5px 10px;
  border-radius: 999px;
  font-size: 11px;
  font-weight: 600;
  color: #fff;
  background: rgba(15, 23, 42, 0.48);
  backdrop-filter: blur(6px);
}

.img-status-badge--ready {
  background: rgba(5, 150, 105, 0.88);
}

.img-status-badge--failed {
  background: rgba(220, 38, 38, 0.84);
}

.img-status-badge--indexing,
.img-status-badge--pending {
  background: rgba(217, 119, 6, 0.84);
}

.img-card-info {
  padding: 14px 14px 16px;
}

.img-card-headline {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 8px;
}

.img-name {
  min-width: 0;
  flex: 1;
  font-size: 14px;
  font-weight: 700;
  color: #0f172a;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.img-file-size {
  flex-shrink: 0;
  font-size: 11px;
  color: #64748b;
}

.img-desc {
  min-height: 40px;
  font-size: 12px;
  line-height: 1.65;
  color: #475569;
  margin-bottom: 10px;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.img-meta {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: #94a3b8;
  margin-bottom: 12px;
}

.img-meta-dot {
  opacity: 0.7;
}

.img-card-actions {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.img-action-btn {
  padding: 7px 10px;
  border-radius: 10px;
  border: 1px solid rgba(148, 163, 184, 0.2);
  background: #fff;
  color: #334155;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.18s ease;
}

.img-action-btn:hover {
  border-color: rgba(14, 165, 233, 0.28);
  color: #0369a1;
}

.img-action-btn--delete {
  background: #fff1f2;
  color: #be123c;
  border-color: rgba(244, 63, 94, 0.18);
}

.img-action-btn--delete:hover {
  border-color: rgba(225, 29, 72, 0.26);
  color: #9f1239;
}

.img-select-hint {
  font-size: 12px;
  color: #2563eb;
  font-weight: 600;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

@keyframes imgPulse {
  from { transform: scale(0.94); opacity: 0.72; }
  to { transform: scale(1); opacity: 1; }
}

@media (max-width: 768px) {
  .img-panel-head,
  .upload-zone-inner,
  .img-toolbar {
    flex-direction: column;
    align-items: stretch;
  }

  .img-panel-metrics,
  .img-toolbar-left,
  .img-toolbar-right {
    justify-content: flex-start;
  }

  .img-grid {
    grid-template-columns: 1fr;
  }

  .img-desc {
    min-height: auto;
  }
}
</style>
