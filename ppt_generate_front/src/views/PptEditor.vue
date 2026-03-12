<template>
  <div class="editor-shell">
    <!-- 顶部工具栏 -->
    <header class="editor-header">
      <div class="header-left">
        <button class="btn ghost" @click="backToHistory">← 返回历史</button>
        <div class="header-title-group">
          <input
            v-model="ppt.title"
            class="ppt-title-input"
            type="text"
            placeholder="演示文稿标题"
          />
          <select v-model="ppt.theme_id" class="theme-select">
            <option v-for="t in themes" :key="t.id" :value="t.id">{{ t.name }}</option>
          </select>
        </div>
      </div>
      <div class="header-right">
        <span class="save-status" :class="saveStatusClass">{{ saveStatusText }}</span>
        <button class="btn" :disabled="saving" @click="handleSave">
          {{ saving ? '保存中...' : '保存草稿' }}
        </button>
        <button class="btn primary" :disabled="regenerating" @click="handleRegenerate">
          {{ regenerating ? '生成中...' : '重新生成并下载' }}
        </button>
      </div>
    </header>

    <main class="editor-main" v-if="loaded">
      <!-- 左侧：幻灯片缩略图列表 -->
      <aside class="slide-panel">
        <div class="slide-panel-header">
          <span>幻灯片（{{ ppt.slides.length }}）</span>
          <button class="mini-btn" @click="addSlide">+ 新增</button>
        </div>
        <div class="slide-list">
          <div
            v-for="(slide, index) in ppt.slides"
            :key="slide.id"
            class="slide-thumb-wrap"
            :class="{ active: index === currentIndex }"
            @click="currentIndex = index"
          >
            <div class="slide-num">{{ index + 1 }}</div>
            <!-- 缩略图预览 -->
            <div
              class="slide-thumb"
              :style="thumbStyle(slide, index)"
            >
              <div class="thumb-inner">
                <!-- 封面布局 -->
                <template v-if="getThumbLayout(slide, index) === 'cover'">
                  <div class="thumb-cover-title" :style="{ color: '#' + currentTheme.accent }">
                    {{ slide.title || ppt.title || '封面' }}
                  </div>
                  <div class="thumb-cover-sub" :style="{ color: '#' + currentTheme.secondary }">
                    {{ slide.subtitle || '' }}
                  </div>
                </template>
                <!-- 章节布局 -->
                <template v-else-if="getThumbLayout(slide, index) === 'section'">
                  <div class="thumb-section-num" :style="{ color: '#' + currentTheme.secondary }">
                    {{ String(index).padStart(2, '0') }}
                  </div>
                  <div class="thumb-section-title" :style="{ color: '#' + currentTheme.accent }">
                    {{ slide.title }}
                  </div>
                </template>
                <!-- 结束页 -->
                <template v-else-if="getThumbLayout(slide, index) === 'closing'">
                  <div class="thumb-closing-title" :style="{ color: '#' + currentTheme.accent }">
                    {{ slide.title || 'Thank You' }}
                  </div>
                </template>
                <!-- 普通内容页 -->
                <template v-else>
                  <div class="thumb-content-title" :style="{ color: '#' + currentTheme.primary }">
                    {{ slide.title }}
                  </div>
                  <!-- 图表标识 -->
                  <div v-if="slide.content?.chart_data" class="thumb-chart-badge">
                    <span>📊</span>
                    <span class="thumb-chart-type">{{ slide.content.chart_data.type }}</span>
                  </div>
                  <!-- 要点列表 -->
                  <template v-else>
                    <div
                      v-for="(b, bi) in (slide.content?.bullets || []).slice(0, 4)"
                      :key="bi"
                      class="thumb-bullet"
                    >
                      <span class="thumb-bullet-dot" :style="{ background: '#' + currentTheme.primary }"></span>
                      <span class="thumb-bullet-text">{{ b }}</span>
                    </div>
                    <div v-if="(slide.content?.bullets || []).length > 4" class="thumb-more">
                      +{{ (slide.content?.bullets || []).length - 4 }} 条...
                    </div>
                  </template>
                  <!-- 图片标识 -->
                  <div v-if="slide.content?.image_url" class="thumb-img-badge">🖼</div>
                </template>
              </div>
            </div>
          </div>
        </div>
      </aside>

      <!-- 右侧：编辑面板 -->
      <section class="edit-panel" v-if="currentSlide">
        <div class="edit-panel-header">
          <div class="edit-panel-title">
            <span class="pill">第 {{ currentIndex + 1 }} 页</span>
            <h3>编辑幻灯片</h3>
          </div>
          <div class="edit-panel-actions">
            <button class="mini-btn" :disabled="currentIndex === 0" @click="moveSlide(-1)">↑ 上移</button>
            <button class="mini-btn" :disabled="currentIndex >= ppt.slides.length - 1" @click="moveSlide(1)">↓ 下移</button>
            <button class="mini-btn danger" :disabled="ppt.slides.length <= 1" @click="removeSlide">删除本页</button>
          </div>
        </div>

        <!-- 布局类型 -->
        <div class="field">
          <label>布局类型</label>
          <select v-model="currentSlide.layout">
            <option v-for="l in layouts" :key="l.id" :value="l.id">{{ l.name }}</option>
          </select>
        </div>

        <!-- 页标题 -->
        <div class="field">
          <label>页标题</label>
          <input v-model="currentSlide.title" type="text" placeholder="请输入该页标题" />
        </div>

        <!-- 副标题（仅封面/章节页显示） -->
        <div class="field" v-if="['cover', 'section', 'closing'].includes(currentSlide.layout)">
          <label>副标题</label>
          <input v-model="currentSlide.subtitle" type="text" placeholder="副标题（可选）" />
        </div>

        <!-- 要点列表（非封面/章节/结束页显示） -->
        <div class="field" v-if="!['cover', 'section', 'closing'].includes(currentSlide.layout)">
          <label>
            要点列表
            <span class="field-hint">（拖拽左侧 ⠿ 可排序）</span>
          </label>
          <div class="bullets-list">
            <div
              v-for="(bullet, bi) in currentSlide.content.bullets"
              :key="bi"
              class="bullet-row"
              draggable="true"
              @dragstart="onBulletDragStart(bi)"
              @dragover.prevent="onBulletDragOver(bi)"
              @drop="onBulletDrop(bi)"
              @dragend="bulletDragIndex = null"
              :class="{ 'drag-over': bulletDragOver === bi }"
            >
              <span class="drag-handle">⠿</span>
              <input
                v-model="currentSlide.content.bullets[bi]"
                type="text"
                class="bullet-input"
                placeholder="输入要点内容"
              />
              <button class="icon-btn danger" @click="removeBullet(bi)" title="删除">✕</button>
            </div>
            <button class="add-bullet-btn" @click="addBullet">+ 添加要点</button>
          </div>
        </div>

        <!-- 图片 URL -->
        <div class="field" v-if="!['cover', 'section', 'closing'].includes(currentSlide.layout)">
          <label>图片 URL（可选）</label>
          <div class="image-field">
            <input
              v-model="currentSlide.content.image_url"
              type="text"
              placeholder="填入图片地址，留空则不显示图片"
            />
            <div v-if="currentSlide.content.image_url" class="image-preview">
              <img
                :src="currentSlide.content.image_url"
                alt="预览"
                @error="onImgError"
                class="img-thumb"
              />
            </div>
          </div>
        </div>

        <!-- 图表数据编辑 -->
        <div class="field chart-field" v-if="!['cover', 'section', 'closing'].includes(currentSlide.layout)">
          <div class="chart-field-header" @click="chartPanelOpen = !chartPanelOpen">
            <label style="cursor:pointer">
              📊 图表数据
              <span class="field-hint">（{{ currentSlide.content.chart_data ? '已配置' : '未配置，点击展开' }}）</span>
            </label>
            <span class="collapse-icon">{{ chartPanelOpen ? '▲' : '▼' }}</span>
          </div>
          <div v-if="chartPanelOpen" class="chart-editor">
            <div class="chart-toggle-row">
              <label class="toggle-label">
                <input type="checkbox" :checked="!!currentSlide.content.chart_data" @change="toggleChart" />
                启用图表
              </label>
            </div>
            <template v-if="currentSlide.content.chart_data">
              <div class="chart-meta-row">
                <div class="field">
                  <label>图表类型</label>
                  <select v-model="currentSlide.content.chart_data.type">
                    <option value="bar">柱状图（bar）</option>
                    <option value="pie">饼图（pie）</option>
                    <option value="line">折线图（line）</option>
                    <option value="doughnut">环形图（doughnut）</option>
                  </select>
                </div>
                <div class="field">
                  <label>图表标题</label>
                  <input v-model="currentSlide.content.chart_data.title" type="text" placeholder="图表标题（可选）" />
                </div>
              </div>
              <div class="chart-items-label">数据项</div>
              <div class="chart-items">
                <div class="chart-item-header">
                  <span>标签</span>
                  <span>数值</span>
                  <span></span>
                </div>
                <div
                  v-for="(item, ci) in currentSlide.content.chart_data.items"
                  :key="ci"
                  class="chart-item-row"
                >
                  <input v-model="item.label" type="text" placeholder="标签" class="chart-label-input" />
                  <input v-model.number="item.value" type="number" placeholder="数值" class="chart-value-input" />
                  <button class="icon-btn danger" @click="removeChartItem(ci)" :disabled="currentSlide.content.chart_data.items.length <= 2">✕</button>
                </div>
                <button class="add-bullet-btn" @click="addChartItem">+ 添加数据项</button>
              </div>
              <!-- ECharts 实时预览 -->
              <div class="chart-preview-wrap">
                <div class="chart-preview-label">预览</div>
                <div ref="chartPreviewEl" class="chart-preview-canvas"></div>
              </div>
            </template>
          </div>
        </div>

        <!-- 备注 -->
        <div class="field">
          <label>备注（讲稿提示，不影响排版）</label>
          <textarea v-model="currentSlide.content.notes" rows="3" placeholder="添加讲稿备注..."></textarea>
        </div>
      </section>
    </main>

    <div v-else class="loading-state">
      <div class="loading-spinner"></div>
      <span>正在加载 PPT 结构，请稍候...</span>
    </div>
  </div>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { onBeforeRouteLeave, useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import * as echarts from 'echarts'
import pptAPI from '@/api/ppt'

const route = useRoute()
const router = useRouter()

const id = computed(() => route.params.id)
const loaded = ref(false)
const saving = ref(false)
const regenerating = ref(false)
const isDirty = ref(false)
const lastSavedAt = ref(null)
const chartPanelOpen = ref(false)
const chartPreviewEl = ref(null)
let chartInstance = null

// 主题定义（与 pptxgen_builder.js THEME_PRESETS 完全一致）
const THEME_COLORS = {
  midnight:   { primary: '1E2761', secondary: 'CADCFC', accent: 'FFFFFF' },
  forest:     { primary: '2C5F2D', secondary: '97BC62', accent: 'F5F5F5' },
  charcoal:   { primary: '36454F', secondary: 'F2F2F2', accent: '212121' },
  coral:      { primary: 'F96167', secondary: 'F9E795', accent: '2F3C7E' },
  teal:       { primary: '028090', secondary: '00A896', accent: '02C39A' },
  ocean:      { primary: '065A82', secondary: '1C7293', accent: '21295C' },
  berry:      { primary: '6D2E46', secondary: 'A26769', accent: 'ECE2D0' },
  sage:       { primary: '84B59F', secondary: '69A297', accent: '50808E' },
  terracotta: { primary: 'B85042', secondary: 'E7E8D1', accent: 'A7BEAE' },
  cherry:     { primary: '990011', secondary: 'FCF6F5', accent: '2F3C7E' },
}

const themes = [
  { id: 'midnight',   name: '午夜蓝（midnight）' },
  { id: 'forest',     name: '森林绿（forest）' },
  { id: 'charcoal',   name: '炭灰（charcoal）' },
  { id: 'coral',      name: '珊瑚红（coral）' },
  { id: 'teal',       name: '青蓝（teal）' },
  { id: 'ocean',      name: '深海（ocean）' },
  { id: 'berry',      name: '莓紫（berry）' },
  { id: 'sage',       name: '鼠尾草（sage）' },
  { id: 'terracotta', name: '赤陶（terracotta）' },
  { id: 'cherry',     name: '樱桃红（cherry）' },
]

const layouts = [
  { id: 'title_content',        name: '标题+内容（默认）' },
  { id: 'content_text_only',    name: '纯文字' },
  { id: 'content_text_image',   name: '文字+图片' },
  { id: 'content_bullets_heavy',name: '多要点' },
  { id: 'content_quote',        name: '引用' },
  { id: 'content_chart',        name: '图表' },
  { id: 'section',              name: '章节分隔页' },
  { id: 'cover',                name: '封面' },
  { id: 'closing',              name: '结束页' },
]

const ppt = reactive({
  title: '',
  theme_id: 'midnight',
  slides: [],
  options: { show_page_number: true, lang: 'zh' }
})

const currentIndex = ref(0)
const currentSlide = computed(() => ppt.slides[currentIndex.value] || null)
const currentTheme = computed(() => THEME_COLORS[ppt.theme_id] || THEME_COLORS.midnight)

// 拖拽排序状态
const bulletDragIndex = ref(null)
const bulletDragOver = ref(null)

// ── 自动保存 ──────────────────────────────────────────────
let autoSaveTimer = null
const saveStatusText = ref('已保存')
const saveStatusClass = ref('saved')

watch(
  ppt,
  () => {
    if (!loaded.value) return
    isDirty.value = true
    saveStatusText.value = '未保存更改'
    saveStatusClass.value = 'dirty'
    clearTimeout(autoSaveTimer)
    autoSaveTimer = setTimeout(doAutoSave, 1500)
  },
  { deep: true }
)

async function doAutoSave() {
  if (!isDirty.value || saving.value) return
  try {
    saving.value = true
    saveStatusText.value = '保存中...'
    saveStatusClass.value = 'saving'
    await pptAPI.saveStructure(id.value, buildPayload())
    isDirty.value = false
    lastSavedAt.value = new Date()
    saveStatusText.value = '已自动保存'
    saveStatusClass.value = 'saved'
  } catch {
    saveStatusText.value = '自动保存失败'
    saveStatusClass.value = 'error'
  } finally {
    saving.value = false
  }
}

onBeforeRouteLeave(async (to, from, next) => {
  if (!isDirty.value) { next(); return }
  try {
    await ElMessageBox.confirm('有未保存的更改，是否在离开前保存？', '提示', {
      confirmButtonText: '保存并离开',
      cancelButtonText: '直接离开',
      distinguishCancelAndClose: true,
      type: 'warning',
    })
    await doAutoSave()
    next()
  } catch (action) {
    if (action === 'cancel') next()
    else next(false)
  }
})

onBeforeUnmount(() => {
  clearTimeout(autoSaveTimer)
  if (chartInstance) { chartInstance.dispose(); chartInstance = null }
})

// ── 图表预览 ──────────────────────────────────────────────
watch(
  () => currentSlide.value?.content?.chart_data,
  () => { if (chartPanelOpen.value) nextTick(renderChartPreview) },
  { deep: true }
)

watch(chartPanelOpen, (open) => {
  if (open) nextTick(renderChartPreview)
  else if (chartInstance) { chartInstance.dispose(); chartInstance = null }
})

watch(currentIndex, () => {
  chartPanelOpen.value = false
})

function renderChartPreview() {
  const cd = currentSlide.value?.content?.chart_data
  if (!cd || !chartPreviewEl.value) return
  if (!chartInstance) {
    chartInstance = echarts.init(chartPreviewEl.value)
  }
  const labels = cd.items.map(i => i.label)
  const values = cd.items.map(i => Number(i.value))
  const theme = currentTheme.value
  const colorPalette = [`#${theme.primary}`, `#${theme.secondary}`, `#${theme.accent}`,
    lightenHex(theme.primary, 40), lightenHex(theme.secondary, 40), lightenHex(theme.accent, 40)]

  let option = {}
  if (cd.type === 'pie' || cd.type === 'doughnut') {
    option = {
      color: colorPalette,
      title: cd.title ? { text: cd.title, left: 'center', textStyle: { fontSize: 12 } } : undefined,
      series: [{
        type: 'pie',
        radius: cd.type === 'doughnut' ? ['40%', '70%'] : '65%',
        data: cd.items.map(i => ({ name: i.label, value: Number(i.value) })),
        label: { fontSize: 10 },
      }]
    }
  } else if (cd.type === 'bar') {
    option = {
      color: colorPalette,
      title: cd.title ? { text: cd.title, textStyle: { fontSize: 12 } } : undefined,
      xAxis: { type: 'category', data: labels, axisLabel: { fontSize: 10 } },
      yAxis: { type: 'value', axisLabel: { fontSize: 10 } },
      series: [{ type: 'bar', data: values }]
    }
  } else if (cd.type === 'line') {
    option = {
      color: colorPalette,
      title: cd.title ? { text: cd.title, textStyle: { fontSize: 12 } } : undefined,
      xAxis: { type: 'category', data: labels, axisLabel: { fontSize: 10 } },
      yAxis: { type: 'value', axisLabel: { fontSize: 10 } },
      series: [{ type: 'line', data: values, smooth: true }]
    }
  }
  chartInstance.setOption(option, true)
}

function lightenHex(hex, amount) {
  const num = parseInt(hex.replace(/^#/, ''), 16)
  const r = Math.min(255, ((num >> 16) & 0xff) + amount)
  const g = Math.min(255, ((num >> 8) & 0xff) + amount)
  const b = Math.min(255, (num & 0xff) + amount)
  return '#' + ((r << 16) | (g << 8) | b).toString(16).padStart(6, '0')
}

// ── 缩略图样式 ────────────────────────────────────────────
function thumbStyle(slide, index) {
  const theme = currentTheme.value
  const layout = getThumbLayout(slide, index)
  const isSection = layout === 'section'
  const isCover = layout === 'cover'
  const isClosing = layout === 'closing'
  const bg = (isCover || isSection || isClosing)
    ? `#${theme.primary}`
    : `#f8fafc`
  return { background: bg }
}

function getThumbLayout(slide, index) {
  const layout = (slide.layout || '').toLowerCase()
  if (layout === 'cover') return 'cover'
  if (layout === 'section') return 'section'
  if (layout === 'closing') return 'closing'
  if (index === 0) return 'cover'
  if (index === ppt.slides.length - 1 && ppt.slides.length > 1) return 'closing'
  return 'content'
}

// ── 要点操作 ──────────────────────────────────────────────
function addBullet() {
  currentSlide.value.content.bullets.push('')
}

function removeBullet(index) {
  currentSlide.value.content.bullets.splice(index, 1)
}

function onBulletDragStart(index) {
  bulletDragIndex.value = index
}

function onBulletDragOver(index) {
  bulletDragOver.value = index
}

function onBulletDrop(toIndex) {
  const fromIndex = bulletDragIndex.value
  if (fromIndex === null || fromIndex === toIndex) {
    bulletDragOver.value = null
    return
  }
  const bullets = currentSlide.value.content.bullets
  const [moved] = bullets.splice(fromIndex, 1)
  bullets.splice(toIndex, 0, moved)
  bulletDragIndex.value = null
  bulletDragOver.value = null
}

// ── 图表操作 ──────────────────────────────────────────────
function toggleChart(e) {
  if (e.target.checked) {
    currentSlide.value.content.chart_data = {
      type: 'bar',
      title: '',
      items: [
        { label: '项目A', value: 40 },
        { label: '项目B', value: 30 },
        { label: '项目C', value: 30 },
      ]
    }
    nextTick(renderChartPreview)
  } else {
    currentSlide.value.content.chart_data = null
    if (chartInstance) { chartInstance.dispose(); chartInstance = null }
  }
}

function addChartItem() {
  currentSlide.value.content.chart_data.items.push({ label: '', value: 0 })
}

function removeChartItem(index) {
  currentSlide.value.content.chart_data.items.splice(index, 1)
}

function onImgError(e) {
  e.target.style.display = 'none'
}

// ── 幻灯片操作 ────────────────────────────────────────────
function genSlideId() {
  return `slide_${Date.now()}_${Math.random().toString(36).slice(2, 7)}`
}

function addSlide() {
  ppt.slides.push({
    id: genSlideId(),
    layout: 'title_content',
    title: '',
    subtitle: '',
    content: { bullets: [], image_url: '', notes: '', chart_data: null }
  })
  currentIndex.value = ppt.slides.length - 1
}

function removeSlide() {
  if (ppt.slides.length <= 1) return
  ppt.slides.splice(currentIndex.value, 1)
  if (currentIndex.value >= ppt.slides.length) {
    currentIndex.value = ppt.slides.length - 1
  }
}

function moveSlide(delta) {
  const from = currentIndex.value
  const to = from + delta
  if (to < 0 || to >= ppt.slides.length) return
  const tmp = ppt.slides[from]
  ppt.slides.splice(from, 1)
  ppt.slides.splice(to, 0, tmp)
  currentIndex.value = to
}

// ── 数据加载与保存 ────────────────────────────────────────
function ensureContent(slide) {
  if (!slide.content || typeof slide.content !== 'object') {
    slide.content = { bullets: [], image_url: '', notes: '', chart_data: null }
  }
  if (!Array.isArray(slide.content.bullets)) slide.content.bullets = []
  if (typeof slide.content.image_url !== 'string') slide.content.image_url = ''
  if (typeof slide.content.notes !== 'string') slide.content.notes = ''
  // chart_data 保持原样（null 或对象）
  if (slide.content.chart_data !== null && typeof slide.content.chart_data !== 'object') {
    slide.content.chart_data = null
  }
}

function normalizeLoadedData(data) {
  ppt.title = data.title || ''
  ppt.theme_id = data.theme_id || 'midnight'
  ppt.options = data.options || { show_page_number: true, lang: 'zh' }
  ppt.slides = Array.isArray(data.slides) ? data.slides.map((s, idx) => {
    const slide = {
      id: s.id || genSlideId(),
      layout: s.layout || 'title_content',
      title: s.title || '',
      subtitle: s.subtitle || '',
      content: s.content ? { ...s.content } : {}
    }
    ensureContent(slide)
    return slide
  }) : []
  if (!ppt.slides.length) {
    ppt.slides.push({
      id: genSlideId(),
      layout: 'cover',
      title: ppt.title || '第一页',
      subtitle: '',
      content: { bullets: [], image_url: '', notes: '', chart_data: null }
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
    isDirty.value = false
    saveStatusText.value = '已保存'
    saveStatusClass.value = 'saved'
  } catch (error) {
    console.error('加载结构失败', error)
    ElMessage.error('加载 PPT 结构失败，请稍后重试')
  }
}

function buildPayload() {
  return {
    title: ppt.title,
    theme_id: ppt.theme_id,
    slides: ppt.slides.map((s, index) => ({
      id: s.id || genSlideId(),
      layout: s.layout || 'title_content',
      title: s.title || '',
      subtitle: s.subtitle || '',
      content: {
        bullets: Array.isArray(s.content?.bullets) ? s.content.bullets.filter(b => b.trim()) : [],
        image_url: s.content?.image_url || '',
        notes: s.content?.notes || '',
        chart_data: s.content?.chart_data || null,
      }
    })),
    options: ppt.options || { show_page_number: true, lang: 'zh' }
  }
}

const handleSave = async () => {
  try {
    saving.value = true
    saveStatusText.value = '保存中...'
    saveStatusClass.value = 'saving'
    await pptAPI.saveStructure(id.value, buildPayload())
    isDirty.value = false
    saveStatusText.value = '已保存'
    saveStatusClass.value = 'saved'
    ElMessage.success('已保存草稿')
  } catch (error) {
    console.error('保存结构失败', error)
    saveStatusText.value = '保存失败'
    saveStatusClass.value = 'error'
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
    isDirty.value = false
    saveStatusText.value = '已保存'
    saveStatusClass.value = 'saved'
    ElMessage.success('PPT 已重新生成，正在下载...')
    const downloadUrl = res.data?.downloadUrl || res.downloadUrl
    if (downloadUrl) {
      await downloadFile(downloadUrl, `${ppt.title || 'presentation'}.pptx`)
    }
  } catch (error) {
    console.error('重新生成失败', error)
    ElMessage.error('重新生成失败，请稍后重试')
  } finally {
    regenerating.value = false
  }
}

async function downloadFile(url, filename) {
  try {
    const token = localStorage.getItem('token') || sessionStorage.getItem('token')
    const headers = token ? { Authorization: `Bearer ${token}` } : {}
    const response = await fetch(url, { headers })
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    const blob = await response.blob()
    const objectUrl = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = objectUrl
    a.download = filename
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(objectUrl)
  } catch {
    // 降级：直接打开链接
    window.open(url, '_blank')
  }
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
/* ── 整体布局 ── */
.editor-shell {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background: radial-gradient(circle at 10% 0%, rgba(191, 219, 254, 0.5), transparent 40%),
    linear-gradient(135deg, #f8fafc, #e5f2ff);
}

/* ── 头部 ── */
.editor-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 20px;
  background: rgba(255, 255, 255, 0.92);
  border-bottom: 1px solid rgba(148, 163, 184, 0.25);
  backdrop-filter: blur(8px);
  gap: 12px;
  flex-wrap: wrap;
  position: sticky;
  top: 0;
  z-index: 10;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 12px;
  flex: 1;
  min-width: 0;
}

.header-title-group {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
  min-width: 0;
}

.ppt-title-input {
  flex: 1;
  min-width: 0;
  border: 1px solid #d1d5db;
  border-radius: 8px;
  padding: 7px 11px;
  font-size: 0.95rem;
  font-weight: 600;
}

.theme-select {
  border: 1px solid #d1d5db;
  border-radius: 8px;
  padding: 7px 11px;
  font-size: 0.88rem;
  background: white;
  white-space: nowrap;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}

.save-status {
  font-size: 0.82rem;
  padding: 3px 10px;
  border-radius: 999px;
}
.save-status.saved    { color: #16a34a; background: #dcfce7; }
.save-status.saving   { color: #ca8a04; background: #fef9c3; }
.save-status.dirty    { color: #9a3412; background: #ffedd5; }
.save-status.error    { color: #dc2626; background: #fee2e2; }

/* ── 主体 ── */
.editor-main {
  display: grid;
  grid-template-columns: 220px 1fr;
  gap: 0;
  flex: 1;
  min-height: 0;
}

/* ── 左侧幻灯片列表 ── */
.slide-panel {
  background: #0f172a;
  color: white;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.slide-panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 12px;
  font-size: 0.85rem;
  border-bottom: 1px solid rgba(255,255,255,0.08);
  flex-shrink: 0;
}

.slide-list {
  overflow-y: auto;
  flex: 1;
  padding: 8px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.slide-thumb-wrap {
  display: flex;
  align-items: flex-start;
  gap: 6px;
  cursor: pointer;
  border-radius: 8px;
  padding: 4px;
  border: 2px solid transparent;
  transition: border-color 0.15s;
}

.slide-thumb-wrap:hover { border-color: rgba(96, 165, 250, 0.5); }
.slide-thumb-wrap.active { border-color: #60a5fa; }

.slide-num {
  font-size: 0.7rem;
  color: #94a3b8;
  width: 16px;
  flex-shrink: 0;
  padding-top: 4px;
  text-align: right;
}

.slide-thumb {
  flex: 1;
  aspect-ratio: 16 / 9;
  border-radius: 4px;
  overflow: hidden;
  position: relative;
}

.thumb-inner {
  position: absolute;
  inset: 0;
  padding: 6px 7px;
  display: flex;
  flex-direction: column;
  gap: 2px;
  overflow: hidden;
}

.thumb-cover-title {
  font-size: 0.55rem;
  font-weight: 700;
  line-height: 1.2;
  margin-top: auto;
  margin-bottom: 1px;
}
.thumb-cover-sub {
  font-size: 0.42rem;
  margin-bottom: auto;
}
.thumb-section-num {
  font-size: 0.9rem;
  font-weight: 700;
  line-height: 1;
  margin-top: auto;
}
.thumb-section-title {
  font-size: 0.5rem;
  font-weight: 600;
  margin-bottom: auto;
}
.thumb-closing-title {
  font-size: 0.55rem;
  font-weight: 700;
  text-align: center;
  margin: auto;
}
.thumb-content-title {
  font-size: 0.5rem;
  font-weight: 700;
  border-bottom: 1px solid rgba(0,0,0,0.1);
  padding-bottom: 2px;
  margin-bottom: 2px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.thumb-bullet {
  display: flex;
  align-items: center;
  gap: 2px;
}
.thumb-bullet-dot {
  width: 3px;
  height: 3px;
  border-radius: 50%;
  flex-shrink: 0;
}
.thumb-bullet-text {
  font-size: 0.38rem;
  color: #475569;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.thumb-more {
  font-size: 0.35rem;
  color: #94a3b8;
}
.thumb-chart-badge {
  display: flex;
  align-items: center;
  gap: 2px;
  font-size: 0.42rem;
  color: #475569;
  margin-top: 2px;
}
.thumb-chart-type {
  font-size: 0.38rem;
  color: #64748b;
}
.thumb-img-badge {
  font-size: 0.5rem;
  position: absolute;
  bottom: 3px;
  right: 4px;
}

/* ── 右侧编辑面板 ── */
.edit-panel {
  background: #f8fafc;
  overflow-y: auto;
  padding: 20px 24px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.edit-panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.edit-panel-title {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.edit-panel-title h3 {
  margin: 0;
  font-size: 1.05rem;
}

.edit-panel-actions {
  display: flex;
  gap: 6px;
}

/* ── 通用表单 ── */
.field {
  display: flex;
  flex-direction: column;
  gap: 5px;
}

label {
  font-size: 0.88rem;
  color: #374151;
  font-weight: 600;
}

.field-hint {
  font-weight: 400;
  color: #9ca3af;
  font-size: 0.8rem;
}

input[type="text"],
input[type="number"],
select,
textarea {
  border: 1px solid #d1d5db;
  border-radius: 8px;
  padding: 8px 11px;
  font-size: 0.93rem;
  background: white;
  transition: border-color 0.15s, box-shadow 0.15s;
}

input:focus,
select:focus,
textarea:focus {
  outline: none;
  border-color: #3b82f6;
  box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.15);
}

textarea {
  resize: vertical;
  min-height: 72px;
}

/* ── 要点列表 ── */
.bullets-list {
  display: flex;
  flex-direction: column;
  gap: 5px;
}

.bullet-row {
  display: flex;
  align-items: center;
  gap: 6px;
  background: white;
  border: 1px solid #e5e7eb;
  border-radius: 8px;
  padding: 4px 8px;
  transition: border-color 0.15s, box-shadow 0.15s;
}

.bullet-row.drag-over {
  border-color: #3b82f6;
  box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.2);
}

.drag-handle {
  cursor: grab;
  color: #9ca3af;
  font-size: 1rem;
  user-select: none;
  flex-shrink: 0;
}

.drag-handle:active { cursor: grabbing; }

.bullet-input {
  flex: 1;
  border: none !important;
  padding: 4px 0 !important;
  font-size: 0.9rem;
  box-shadow: none !important;
}

.bullet-input:focus {
  outline: none;
  box-shadow: none !important;
  border: none !important;
}

.add-bullet-btn {
  align-self: flex-start;
  border: 1px dashed #d1d5db;
  background: transparent;
  color: #6b7280;
  border-radius: 8px;
  padding: 6px 14px;
  font-size: 0.85rem;
  cursor: pointer;
  transition: all 0.15s;
}

.add-bullet-btn:hover {
  border-color: #3b82f6;
  color: #3b82f6;
  background: #eff6ff;
}

/* ── 图片预览 ── */
.image-field {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.image-preview {
  border-radius: 8px;
  overflow: hidden;
  border: 1px solid #e5e7eb;
  max-height: 140px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #f9fafb;
}

.img-thumb {
  max-width: 100%;
  max-height: 140px;
  object-fit: contain;
}

/* ── 图表编辑 ── */
.chart-field {
  background: white;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 12px 14px;
}

.chart-field-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  cursor: pointer;
  user-select: none;
}

.chart-field-header label { cursor: pointer; }

.collapse-icon {
  font-size: 0.75rem;
  color: #9ca3af;
}

.chart-editor {
  margin-top: 10px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.chart-toggle-row {
  display: flex;
  align-items: center;
}

.toggle-label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 0.88rem;
  font-weight: 500;
  cursor: pointer;
}

.chart-meta-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

.chart-items-label {
  font-size: 0.85rem;
  font-weight: 600;
  color: #374151;
}

.chart-items {
  display: flex;
  flex-direction: column;
  gap: 5px;
}

.chart-item-header {
  display: grid;
  grid-template-columns: 1fr 100px 28px;
  gap: 6px;
  font-size: 0.78rem;
  color: #9ca3af;
  padding: 0 4px;
}

.chart-item-row {
  display: grid;
  grid-template-columns: 1fr 100px 28px;
  gap: 6px;
  align-items: center;
}

.chart-label-input { }
.chart-value-input { }

.chart-preview-wrap {
  border-top: 1px solid #f3f4f6;
  padding-top: 10px;
}

.chart-preview-label {
  font-size: 0.8rem;
  color: #9ca3af;
  margin-bottom: 6px;
}

.chart-preview-canvas {
  width: 100%;
  height: 200px;
  border-radius: 8px;
  overflow: hidden;
}

/* ── 按钮 ── */
.btn {
  padding: 8px 16px;
  border-radius: 999px;
  border: 1px solid #d1d5db;
  background: white;
  color: #1f2937;
  font-size: 0.88rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s;
  white-space: nowrap;
}

.btn:hover { background: #f3f4f6; }

.btn.primary {
  background: linear-gradient(135deg, #2563eb, #3b82f6);
  color: white;
  border-color: transparent;
}

.btn.primary:hover { box-shadow: 0 4px 12px rgba(59, 130, 246, 0.35); }

.btn.ghost { background: transparent; border-color: transparent; }
.btn.ghost:hover { background: #f3f4f6; }

.btn:disabled { opacity: 0.55; cursor: not-allowed; box-shadow: none; }

.mini-btn {
  border: 1px solid #d1d5db;
  background: white;
  color: #374151;
  border-radius: 6px;
  padding: 4px 10px;
  font-size: 0.8rem;
  cursor: pointer;
  transition: all 0.15s;
  white-space: nowrap;
}

.mini-btn:hover { background: #f3f4f6; }
.mini-btn:disabled { opacity: 0.45; cursor: not-allowed; }
.mini-btn.danger { border-color: #fca5a5; color: #dc2626; }
.mini-btn.danger:hover { background: #fee2e2; }

.icon-btn {
  width: 26px;
  height: 26px;
  border-radius: 6px;
  border: 1px solid #e5e7eb;
  background: white;
  cursor: pointer;
  font-size: 0.75rem;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: all 0.15s;
}

.icon-btn.danger { color: #dc2626; border-color: #fca5a5; }
.icon-btn.danger:hover { background: #fee2e2; }
.icon-btn:disabled { opacity: 0.4; cursor: not-allowed; }

/* ── 其他 ── */
.pill {
  display: inline-flex;
  align-items: center;
  padding: 2px 10px;
  border-radius: 999px;
  background: #eff6ff;
  font-size: 0.75rem;
  color: #2563eb;
  font-weight: 500;
}

.loading-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  flex: 1;
  color: #6b7280;
  font-size: 0.95rem;
  padding: 80px 0;
}

.loading-spinner {
  width: 32px;
  height: 32px;
  border: 3px solid #e5e7eb;
  border-top-color: #3b82f6;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin { to { transform: rotate(360deg); } }

/* ── 响应式 ── */
@media (max-width: 900px) {
  .editor-main {
    grid-template-columns: 1fr;
  }
  .slide-panel {
    max-height: 220px;
  }
  .slide-list {
    flex-direction: row;
    overflow-x: auto;
    overflow-y: hidden;
    padding: 8px;
  }
  .slide-thumb-wrap {
    flex-direction: column;
    align-items: center;
    min-width: 100px;
  }
  .slide-num {
    padding-top: 0;
  }
}
</style>
