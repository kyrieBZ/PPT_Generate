<template>
  <div class="editor-shell">
    <header class="editor-header">
      <div>
        <h2>在线微调 PPT</h2>
        <p class="subtitle">基于已生成的 PPT，支持标题与每页要点的小幅调整。</p>
      </div>
      <div class="header-actions">
        <button class="btn ghost" @click="backToHistory">返回历史</button>
        <button class="btn" :disabled="saving" @click="handleSave">
          {{ saving ? '保存中...' : '保存草稿' }}
        </button>
        <button class="btn primary" :disabled="regenerating" @click="handleRegenerate">
          {{ regenerating ? '重新生成中...' : '重新生成并下载' }}
        </button>
      </div>
    </header>

    <main class="editor-main" v-if="loaded">
      <section class="ppt-meta">
        <div class="field">
          <label>演示文稿标题</label>
          <input v-model="ppt.title" type="text" placeholder="请输入 PPT 标题" />
        </div>
        <div class="field">
          <label>主题（Theme）</label>
          <select v-model="ppt.theme_id">
            <option v-for="t in themes" :key="t.id" :value="t.id">
              {{ t.name }}
            </option>
          </select>
        </div>
      </section>

      <section class="editor-body">
        <aside class="slide-list">
          <div class="slide-list-header">
            <span>幻灯片（{{ ppt.slides.length }}）</span>
            <button class="mini-btn" @click="addSlide">新增一页</button>
          </div>
          <div class="slide-items">
            <button
              v-for="(slide, index) in ppt.slides"
              :key="slide.id || index"
              class="slide-item"
              :class="{ active: index === currentIndex }"
              @click="currentIndex = index"
            >
              <div class="slide-index">第 {{ index + 1 }} 页</div>
              <div class="slide-title">{{ slide.title || '未命名标题' }}</div>
              <div class="slide-bullets-preview">
                <span v-for="(b, i) in (slide.content?.bullets || []).slice(0, 2)" :key="i">
                  • {{ b }}
                </span>
                <span v-if="(slide.content?.bullets || []).length > 2">…</span>
              </div>
            </button>
          </div>
        </aside>

        <section class="slide-editor" v-if="currentSlide">
          <header class="slide-editor-header">
            <div>
              <div class="pill">第 {{ currentIndex + 1 }} 页</div>
              <h3>幻灯片内容</h3>
            </div>
            <div class="slide-editor-actions">
              <button class="mini-btn" :disabled="currentIndex === 0" @click="moveSlide(-1)">上移</button>
              <button
                class="mini-btn"
                :disabled="currentIndex >= ppt.slides.length - 1"
                @click="moveSlide(1)"
              >
                下移
              </button>
              <button class="mini-btn danger" :disabled="ppt.slides.length <= 1" @click="removeSlide">
                删除本页
              </button>
            </div>
          </header>

          <div class="field">
            <label>页标题</label>
            <input v-model="currentSlide.title" type="text" placeholder="请输入该页标题" />
          </div>

          <div class="field">
            <label>副标题（可选）</label>
            <input v-model="currentSlide.subtitle" type="text" placeholder="可为空" />
          </div>

          <div class="field">
            <label>要点列表（每行一个）</label>
            <textarea
              v-model="bulletsText"
              rows="8"
              placeholder="每行一个要点，如：&#10;效率提升&#10;自动排版&#10;风格统一"
              @input="onBulletsInput"
            ></textarea>
          </div>

          <div class="field-inline">
            <div class="field">
              <label>图片 URL（可选）</label>
              <input
                v-model="currentSlide.content.image_url"
                type="text"
                placeholder="可留空，或填一张代表性图片地址"
              />
            </div>
          </div>

          <div class="field">
            <label>备注（可选，仅为讲稿提示）</label>
            <textarea
              v-model="currentSlide.content.notes"
              rows="3"
              placeholder="添加对该页内容的讲解提示，不会影响主体排版"
            ></textarea>
          </div>
        </section>
      </section>
    </main>

    <div v-else class="loading-state">正在加载 PPT 结构，请稍候...</div>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import pptAPI from '@/api/ppt'

const route = useRoute()
const router = useRouter()

const id = computed(() => route.params.id)
const loaded = ref(false)
const saving = ref(false)
const regenerating = ref(false)

// 与后端约定的 schema
const ppt = reactive({
  title: '',
  theme_id: 'business',
  slides: [],
  options: {
    show_page_number: true,
    lang: 'zh'
  }
})

const themes = [
  { id: 'business', name: '商务（business）' },
  { id: 'academic', name: '学术（academic）' },
  { id: 'creative', name: '创意（creative）' },
  { id: 'minimal', name: '简约（minimal）' }
]

const currentIndex = ref(0)
const currentSlide = computed(() => ppt.slides[currentIndex.value] || null)

const bulletsText = ref('')
watch(
  currentSlide,
  (slide) => {
    if (!slide) {
      bulletsText.value = ''
      return
    }
    const bullets = slide.content?.bullets || []
    bulletsText.value = bullets.join('\n')
  },
  { immediate: true }
)

const onBulletsInput = () => {
  if (!currentSlide.value) return
  const lines = bulletsText.value
    .split('\n')
    .map((l) => l.trim())
    .filter(Boolean)
  currentSlide.value.content.bullets = lines
}

const ensureContentObject = (slide) => {
  if (!slide.content || typeof slide.content !== 'object') {
    slide.content = {
      bullets: [],
      image_url: '',
      notes: ''
    }
  }
  if (!Array.isArray(slide.content.bullets)) {
    slide.content.bullets = []
  }
  if (typeof slide.content.image_url !== 'string') {
    slide.content.image_url = ''
  }
  if (typeof slide.content.notes !== 'string') {
    slide.content.notes = ''
  }
}

const normalizeLoadedData = (data) => {
  ppt.title = data.title || ''
  ppt.theme_id = data.theme_id || 'business'
  ppt.options = data.options || { show_page_number: true, lang: 'zh' }
  ppt.slides = Array.isArray(data.slides) ? data.slides.map((s, idx) => {
    const slide = {
      id: s.id || `slide_${idx + 1}`,
      layout: s.layout || 'title_content',
      title: s.title || '',
      subtitle: s.subtitle || '',
      content: s.content || {}
    }
    ensureContentObject(slide)
    return slide
  }) : []
  if (!ppt.slides.length) {
    ppt.slides.push({
      id: 'slide_1',
      layout: 'title_content',
      title: ppt.title || '第一页',
      subtitle: '',
      content: {
        bullets: [],
        image_url: '',
        notes: ''
      }
    })
  }
  currentIndex.value = 0
}

const loadStructure = async () => {
  try {
    loaded.value = false
    const res = await pptAPI.getStructure(id.value)
    normalizeLoadedData(res.data || res)
    loaded.value = true
  } catch (error) {
    console.error('加载结构失败', error)
    ElMessage.error('加载 PPT 结构失败，请稍后重试')
  }
}

const buildPayload = () => {
  return {
    title: ppt.title,
    theme_id: ppt.theme_id,
    slides: ppt.slides.map((s, index) => ({
      id: s.id || `slide_${index + 1}`,
      layout: s.layout || 'title_content',
      title: s.title || '',
      subtitle: s.subtitle || '',
      content: {
        bullets: Array.isArray(s.content?.bullets) ? s.content.bullets : [],
        image_url: s.content?.image_url || '',
        notes: s.content?.notes || ''
      }
    })),
    options: ppt.options || { show_page_number: true, lang: 'zh' }
  }
}

const handleSave = async () => {
  try {
    saving.value = true
    const payload = buildPayload()
    await pptAPI.saveStructure(id.value, payload)
    ElMessage.success('已保存草稿')
  } catch (error) {
    console.error('保存结构失败', error)
    ElMessage.error('保存失败，请稍后重试')
  } finally {
    saving.value = false
  }
}

const handleRegenerate = async () => {
  try {
    regenerating.value = true
    const payload = buildPayload()
    const res = await pptAPI.regenerateFromStructure(id.value, payload)
    ElMessage.success('已重新生成 PPT，正在下载...')
    const downloadUrl = res.data?.downloadUrl || res.downloadUrl
    if (downloadUrl) {
      const link = document.createElement('a')
      link.href = downloadUrl
      link.rel = 'noopener'
      link.target = '_blank'
      document.body.appendChild(link)
      link.click()
      document.body.removeChild(link)
    }
  } catch (error) {
    console.error('重新生成失败', error)
    ElMessage.error('重新生成失败，请稍后重试')
  } finally {
    regenerating.value = false
  }
}

const addSlide = () => {
  const index = ppt.slides.length + 1
  ppt.slides.push({
    id: `slide_${index}`,
    layout: 'title_content',
    title: '',
    subtitle: '',
    content: {
      bullets: [],
      image_url: '',
      notes: ''
    }
  })
  currentIndex.value = ppt.slides.length - 1
}

const removeSlide = () => {
  if (ppt.slides.length <= 1) return
  ppt.slides.splice(currentIndex.value, 1)
  if (currentIndex.value >= ppt.slides.length) {
    currentIndex.value = ppt.slides.length - 1
  }
}

const moveSlide = (delta) => {
  const from = currentIndex.value
  const to = from + delta
  if (to < 0 || to >= ppt.slides.length) return
  const tmp = ppt.slides[from]
  ppt.slides.splice(from, 1)
  ppt.slides.splice(to, 0, tmp)
  currentIndex.value = to
}

const backToHistory = () => {
  router.push('/main/history')
}

onMounted(() => {
  if (!id.value) {
    ElMessage.error('缺少 PPT ID，无法进入编辑')
    router.push('/main/history')
    return
  }
  loadStructure()
})
</script>

<style scoped>
.editor-shell {
  min-height: 100vh;
  padding: 24px;
  background: radial-gradient(circle at 10% 0%, rgba(191, 219, 254, 0.6), transparent 40%),
    linear-gradient(135deg, #f8fafc, #e5f2ff);
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.editor-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
}

.editor-header h2 {
  font-size: 1.6rem;
  margin-bottom: 4px;
}

.subtitle {
  color: #64748b;
  font-size: 0.95rem;
}

.header-actions {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.btn {
  padding: 8px 16px;
  border-radius: 999px;
  border: 1px solid #cbd5f5;
  background: white;
  color: #1f2937;
  font-weight: 500;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  transition: all 0.2s ease;
}

.btn:hover {
  background: #eef2ff;
}

.btn.primary {
  background: linear-gradient(135deg, #0ea5e9, #38bdf8);
  color: white;
  border-color: transparent;
}

.btn.primary:hover {
  box-shadow: 0 12px 24px rgba(56, 189, 248, 0.25);
}

.btn.ghost {
  background: transparent;
}

.btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
  box-shadow: none;
}

.editor-main {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.ppt-meta {
  display: flex;
  gap: 16px;
  padding: 16px 18px;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.9);
  border: 1px solid rgba(148, 163, 184, 0.3);
}

.ppt-meta .field {
  flex: 1;
}

.editor-body {
  display: grid;
  grid-template-columns: 260px minmax(0, 1fr);
  gap: 16px;
}

.slide-list {
  background: rgba(15, 23, 42, 0.96);
  color: white;
  border-radius: 16px;
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.slide-list-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 0.9rem;
}

.mini-btn {
  border: 1px solid rgba(148, 163, 184, 0.8);
  background: transparent;
  color: inherit;
  border-radius: 999px;
  padding: 4px 10px;
  font-size: 0.8rem;
  cursor: pointer;
}

.mini-btn.danger {
  border-color: #f97373;
  color: #fecaca;
}

.slide-items {
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-top: 4px;
}

.slide-item {
  text-align: left;
  border-radius: 10px;
  padding: 8px 10px;
  border: 1px solid transparent;
  background: rgba(15, 23, 42, 0.95);
  cursor: pointer;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.slide-item:hover {
  border-color: rgba(96, 165, 250, 0.9);
}

.slide-item.active {
  background: linear-gradient(135deg, #0f172a, #1d4ed8);
  border-color: #60a5fa;
}

.slide-index {
  font-size: 0.8rem;
  color: #a5b4fc;
}

.slide-title {
  font-size: 0.9rem;
  font-weight: 500;
  white-space: nowrap;
  text-overflow: ellipsis;
  overflow: hidden;
}

.slide-bullets-preview {
  font-size: 0.75rem;
  color: #cbd5f5;
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
}

.slide-editor {
  background: rgba(255, 255, 255, 0.96);
  border-radius: 16px;
  padding: 18px 20px;
  border: 1px solid rgba(148, 163, 184, 0.3);
  box-shadow: 0 15px 35px rgba(15, 23, 42, 0.08);
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.slide-editor-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 4px;
}

.slide-editor-header h3 {
  margin-top: 4px;
}

.pill {
  display: inline-flex;
  align-items: center;
  padding: 3px 10px;
  border-radius: 999px;
  background: #eef2ff;
  font-size: 0.75rem;
  color: #4338ca;
}

.slide-editor-actions {
  display: flex;
  gap: 6px;
}

.field {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.field-inline {
  display: flex;
  gap: 12px;
}

label {
  font-size: 0.9rem;
  color: #4b5563;
  font-weight: 500;
}

input,
textarea,
select {
  border-radius: 10px;
  border: 1px solid #d1d5db;
  padding: 9px 11px;
  font-size: 0.95rem;
  transition: border-color 0.15s ease, box-shadow 0.15s ease;
}

input:focus,
textarea:focus,
select:focus {
  outline: none;
  border-color: #0ea5e9;
  box-shadow: 0 0 0 1px rgba(56, 189, 248, 0.4);
}

textarea {
  resize: vertical;
  min-height: 100px;
}

.loading-state {
  margin-top: 60px;
  text-align: center;
  color: #6b7280;
}

@media (max-width: 960px) {
  .editor-body {
    grid-template-columns: 1fr;
  }
}
</style>

