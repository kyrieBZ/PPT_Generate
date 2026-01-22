<template>
  <div class="main-container">
    <aside class="sidebar">
      <div class="sidebar-header">
        <h2>PPT智能生成系统</h2>
        <div class="user-info">
          <div class="avatar">{{ userInitials }}</div>
          <div class="user-details">
            <p class="username">{{ currentUser?.username }}</p>
            <p class="user-email">{{ currentUser?.email }}</p>
          </div>
        </div>
      </div>

      <nav class="sidebar-nav">
        <router-link
          v-for="item in menuItems"
          :key="item.id"
          :to="item.path"
          class="nav-item"
          :class="{ 'active': activeMenu === item.id }"
          @click="activeMenu = item.id"
        >
          <span class="nav-icon">{{ item.icon }}</span>
          <span class="nav-text">{{ item.text }}</span>
        </router-link>
      </nav>

      <div class="sidebar-footer">
        <button @click="handleLogout" class="logout-btn">
          <span class="logout-icon">🚪</span>
          <span>退出登录</span>
        </button>
      </div>
    </aside>

    <main class="main-content">
      <header class="main-header">
        <div class="header-left">
          <h1>{{ activeMenuItem?.text || 'PPT智能生成系统' }}</h1>
          <p class="page-description">{{ activeMenuItem?.description || '基于GenAI的智能PPT生成平台' }}</p>
        </div>
        <div class="header-right">
          <button class="generate-btn" @click="showGenerateModal = true">
            <span class="btn-icon">✨</span>
            <span>新建PPT</span>
          </button>
        </div>
      </header>

      <div class="content-area">
        <section v-if="activeMenu === 'dashboard'" class="dashboard">
          <div class="stats-grid">
            <div v-for="card in statCards" :key="card.id" class="stat-card">
              <div class="stat-icon">{{ card.icon }}</div>
              <div class="stat-content">
                <h3>{{ card.title }}</h3>
                <p class="stat-number">{{ card.value }}</p>
              </div>
            </div>
          </div>

          <div class="features-grid">
            <div class="feature-card">
              <div class="feature-icon">🤖</div>
              <h4>智能生成</h4>
              <p>基于GenAI模型自动生成PPT内容</p>
              <button class="feature-action" @click="showGenerateModal = true">
                <span class="btn-content">
                  <el-icon class="btn-icon"><MagicStick /></el-icon>
                  <span>开始生成</span>
                </span>
              </button>
            </div>
            <div class="feature-card">
              <div class="feature-icon">🎨</div>
              <h4>个性化定制</h4>
              <p>多通道指令满足个性化需求</p>
              <button class="feature-action">查看模板</button>
            </div>
            <div class="feature-card">
              <div class="feature-icon">📊</div>
              <h4>图文表生成</h4>
              <p>智能生成文本、图片和表格</p>
              <button class="feature-action">使用示例</button>
            </div>
          </div>
        </section>

        <section v-if="activeMenu === 'generate'" class="generate-section">
          <div class="generate-panel">
            <h2>最新生成预览</h2>
            <p>完成生成任务后将在此展示生成的PPT预览。</p>
            <div v-if="selectedTemplate" class="template-hint">
              <span>当前模板：{{ selectedTemplate.name }} · {{ selectedTemplate.provider }}</span>
              <a
                v-if="selectedTemplate.localDownloadUrl"
                class="template-download-link"
                :href="selectedTemplate.localDownloadUrl"
                download
              >
                下载内置模板
              </a>
            </div>
            <div
              v-if="previewEmbedUrl || hasLocalPreview"
              class="preview-card"
              :class="{ 'preview-has-bg': Boolean(previewCardStyle.backgroundImage) }"
              :style="previewCardStyle"
            >
              <div class="preview-header">
                <div class="preview-label">PPT 预览</div>
                <div class="preview-actions">
                  <div v-if="previewMode === 'local' && hasLocalPreview" class="preview-counter">
                    第 {{ previewIndex + 1 }} / {{ previewSlideCount }} 页
                  </div>
                  <div class="preview-mode">
                    <button
                      class="preview-toggle"
                      :class="{ active: previewMode === 'online' }"
                      :disabled="!canUseOnlinePreview"
                      @click="setPreviewMode('online')"
                    >
                      在线预览
                    </button>
                    <button
                      class="preview-toggle"
                      :class="{ active: previewMode === 'local' }"
                      :disabled="!canUseLocalPreview"
                      @click="setPreviewMode('local')"
                    >
                      本地预览
                    </button>
                  </div>
                </div>
              </div>
              <div v-if="previewMode === 'online'" class="preview-embed">
                <iframe
                  v-if="previewEmbedUrl"
                  class="preview-iframe"
                  :src="previewEmbedUrl"
                  title="PPT预览"
                  frameborder="0"
                  allowfullscreen
                ></iframe>
                <div v-else class="preview-placeholder">在线预览不可用，请切换到本地预览。</div>
              </div>
              <div v-else class="preview-body" :class="previewLayoutClass">
                <template v-if="hasLocalPreview">
                  <div class="layout-grid">
                    <div class="layout-text">
                      <h3>{{ currentPreviewSlide?.title || '未命名标题' }}</h3>
                      <ul v-if="currentPreviewBullets.length">
                        <li v-for="(item, index) in currentPreviewBullets" :key="`${previewIndex}-bullet-${index}`">
                          {{ item }}
                        </li>
                      </ul>
                      <div v-else-if="currentPreviewRawText" class="preview-raw">
                        {{ currentPreviewRawText }}
                      </div>
                      <div v-else class="preview-placeholder">暂无正文内容</div>
                      <div v-if="currentPreviewNotes" class="preview-raw">备注：{{ currentPreviewNotes }}</div>
                      <div v-if="currentPreviewSuggestions.length" class="image-prompts">
                        <span v-for="(item, index) in currentPreviewSuggestions" :key="`${previewIndex}-suggestion-${index}`">
                          {{ item }}
                        </span>
                      </div>
                    </div>
                    <div v-if="hasPreviewMedia" class="layout-media">
                      <div v-if="currentPreviewImages.length" class="preview-images">
                        <img
                          v-for="(url, index) in currentPreviewImages"
                          :key="`${previewIndex}-img-${index}`"
                          :src="url"
                          alt="预览图片"
                          loading="lazy"
                        >
                      </div>
                      <div v-else-if="previewFallbackImage" class="preview-template-fallback">
                        <img :src="previewFallbackImage" alt="模板预览">
                      </div>
                      <div v-if="currentPreviewPrompts.length" class="image-prompts">
                        <span v-for="(item, index) in currentPreviewPrompts" :key="`${previewIndex}-prompt-${index}`">
                          {{ item }}
                        </span>
                      </div>
                    </div>
                  </div>
                  <div class="preview-controls">
                    <button class="preview-nav" :disabled="previewIndex === 0" @click="goPreviewPrev">上一页</button>
                    <button class="preview-nav" :disabled="previewIndex >= previewSlideCount - 1" @click="goPreviewNext">
                      下一页
                    </button>
                  </div>
                  <div class="preview-thumbnails">
                    <button
                      v-for="(slide, index) in previewSlides"
                      :key="`thumb-${index}`"
                      class="preview-thumb"
                      :class="{ active: index === previewIndex }"
                      @click="selectPreviewSlide(index)"
                    >
                      <span class="thumb-index">{{ index + 1 }}</span>
                      <span>{{ slide.title || '未命名' }}</span>
                    </button>
                  </div>
                </template>
                <div v-else class="preview-placeholder">本地预览暂无数据，请先生成PPT。</div>
              </div>
            </div>
            <div v-else class="preview-empty">暂无PPT预览，请先生成PPT。</div>
          </div>
        </section>

        <section v-if="activeMenu === 'history'" class="history-section">
          <div class="section-header">
            <h2>生成历史</h2>
            <div class="search-box history-search-box">
              <el-input
                v-model="historyQuery"
                placeholder="搜索标题或主题..."
                clearable
                @keyup.enter="triggerHistorySearch"
              >
                <template #prefix>
                  <el-icon><Search /></el-icon>
                </template>
              </el-input>
              <el-button
                class="search-icon-btn"
                circle
                :icon="Search"
                @click="triggerHistorySearch"
              />
            </div>
          </div>

          <div v-if="historyBusy" class="history-empty">{{ historyBusyLabel }}</div>
          <div v-else-if="!filteredHistory.length" class="history-empty">暂无记录，立即生成第一份PPT吧！</div>
          <div v-else class="history-list">
            <div v-for="item in pagedHistory" :key="item.id" class="history-item">
              <div class="history-preview">
                <div class="preview-icon">📄</div>
                <div class="preview-content">
                  <h4>{{ item.title }}</h4>
                  <p>{{ item.topic }}</p>
                  <div class="history-meta">
                    <span class="meta-item">生成时间: {{ formatDate(item.createdAt) }}</span>
                    <span class="meta-item">页数: {{ item.pages }}</span>
                    <span class="meta-item">状态: {{ item.status }}</span>
                    <span class="meta-item">模板: {{ item.templateName || '未选择' }}</span>
                  </div>
                </div>
              </div>
              <div class="history-actions">
                <el-button size="large" type="primary" @click="editPPT(item)">
                  <span class="btn-content">
                    <el-icon class="btn-icon"><EditPen /></el-icon>
                    <span>编辑</span>
                  </span>
                </el-button>
                <el-button size="large" type="success" @click="downloadPPT(item)">
                  <span class="btn-content">
                    <el-icon class="btn-icon"><Download /></el-icon>
                    <span>下载</span>
                  </span>
                </el-button>
                <el-button size="large" type="danger" @click="deleteHistory(item)">
                  <span class="btn-content">
                    <el-icon class="btn-icon"><Delete /></el-icon>
                    <span>删除</span>
                  </span>
                </el-button>
              </div>
            </div>
          </div>
          <div v-if="showHistoryPagination && !historyBusy" class="history-pagination">
            <el-pagination
              background
              layout="total, sizes, prev, pager, next"
              :total="historyTotal"
              :page-size="historyPageSize"
              :page-sizes="[6, 10, 15, 20]"
              :current-page="historyPage"
              @current-change="handleHistoryPageChange"
              @size-change="handleHistorySizeChange"
            />
          </div>
        </section>

        <section v-if="activeMenu === 'templates'" class="templates-section">
          <div class="section-header">
            <h2>模板中心</h2>
            <div class="search-box template-search-box">
              <el-input
                v-model="templateQuery"
                placeholder="搜索模板名称、标签或来源..."
                clearable
                @keyup.enter="triggerTemplateSearch"
              >
                <template #prefix>
                  <el-icon><Search /></el-icon>
                </template>
              </el-input>
              <el-button
                class="search-icon-btn"
                circle
                :icon="Search"
                :loading="templateSearchLoading"
                @click="triggerTemplateSearch"
              />
            </div>
          </div>

          <div v-if="templatesBusy" class="templates-empty">{{ templatesBusyLabel }}</div>
          <div v-else-if="!filteredTemplates.length" class="templates-empty">
            暂无匹配的模板，稍后再试或更换关键词。
          </div>
          <div v-else class="templates-grid">
            <div v-for="template in filteredTemplates" :key="template.id" class="template-card">
              <div class="template-preview">
                <img :src="template.previewImage" :alt="template.name" v-if="template.previewImage" loading="lazy">
                <div class="default-preview" v-else>
                  <div class="preview-slides">
                    <div class="slide" v-for="n in 3" :key="n"></div>
                  </div>
                </div>
              </div>
              <div class="template-info">
                <div class="template-title-row">
                  <h4>{{ template.name }}</h4>
                  <span v-if="template.localDownloadUrl" class="template-badge">内置模板</span>
                </div>
                <p class="template-desc">{{ template.description }}</p>
                <div class="template-meta">
                  <span>来源：
                    <a :href="template.providerUrl" target="_blank" rel="noopener">
                      {{ template.provider }}
                    </a>
                  </span>
                  <span>授权：{{ template.license }}</span>
                </div>
                <div v-if="template.tags?.length" class="template-tags">
                  <span v-for="tag in template.tags" :key="tag" class="template-tag">#{{ tag }}</span>
                </div>
                <div class="template-actions">
                  <button class="use-template-btn" @click="useTemplate(template)">快速填充</button>
                  <a
                    v-if="template.localDownloadUrl"
                    class="use-template-btn outline"
                    :href="template.localDownloadUrl"
                    download
                  >
                    下载内置
                  </a>
                  <a
                    class="use-template-btn secondary"
                    :href="template.downloadUrl"
                    target="_blank"
                    rel="noopener"
                  >
                    打开源站
                  </a>
                </div>
              </div>
            </div>
          </div>
        </section>

        <section v-if="activeMenu === 'settings'" class="settings-section">
          <div class="settings-card">
            <h2>系统设置</h2>
            <p class="settings-desc">选择默认生成模型，并规划未来自研模型的数据准备。</p>

            <div class="settings-block">
              <h3>默认生成模型</h3>
              <p>根据使用场景选择对应模型，保存后生成页会自动采用该模型。</p>
              <div v-if="modelsLoading" class="templates-empty">模型加载中...</div>
              <div v-else class="settings-models">
                <label
                  v-for="model in models"
                  :key="model.id"
                  class="settings-model"
                  :class="{ selected: selectedModel === model.id }"
                >
                  <input
                    type="radio"
                    :value="model.id"
                    :checked="selectedModel === model.id"
                    @change="handleModelChange(model.id)"
                  />
                  <div class="model-texts">
                    <strong>{{ model.name }}</strong>
                    <small>{{ model.provider }} · {{ model.locale }}</small>
                    <p>{{ model.description }}</p>
                    <div class="model-tags">
                      <span v-if="model.latency">延迟: {{ model.latency }}</span>
                      <span v-if="model.cost">成本: {{ model.cost }}</span>
                    </div>
                  </div>
                </label>
              </div>
            </div>

            <div class="settings-block">
              <h3>自研模型计划</h3>
              <p>记录自建PPT语料与训练想法，便于后续接入 Future PPT Lab 模型。</p>
              <div class="plan-fields">
                <label>
                  数据集名称 / 存储路径
                  <input type="text" v-model="futurePlan.dataset" placeholder="例：/datasets/ppt_corp_v1" />
                </label>
                <label>
                  备注
                  <textarea v-model="futurePlan.notes" rows="4" placeholder="记录采样、清洗、标注等规划..."></textarea>
                </label>
              </div>
              <small>该信息仅保存在浏览器本地，方便随时查看。</small>
            </div>
          </div>
        </section>
      </div>
    </main>

    <div v-if="showGenerateModal" class="modal-overlay" @click.self="showGenerateModal = false">
      <div class="modal-content">
        <div class="modal-header">
          <h2>智能PPT生成</h2>
          <button class="modal-close" @click="showGenerateModal = false">×</button>
        </div>

        <div class="modal-body">
          <div class="generate-form">
            <div class="form-group">
              <label for="ppt-title">PPT标题</label>
              <input
                id="ppt-title"
                v-model="generateForm.title"
                type="text"
                placeholder="请输入PPT标题"
              />
            </div>

            <div class="form-group">
              <label for="ppt-topic">主题/内容描述</label>
              <textarea
                id="ppt-topic"
                v-model="generateForm.topic"
                placeholder="请详细描述PPT的主题和内容要求..."
                rows="4"
              ></textarea>
            </div>

            <div class="form-group">
              <label for="ppt-pages">期望页数</label>
              <input
                id="ppt-pages"
                v-model.number="generateForm.pages"
                type="number"
                min="1"
                max="50"
              />
            </div>

            <div class="form-group">
              <label>模板风格</label>
              <div class="style-options">
                <label
                  v-for="style in styles"
                  :key="style.id"
                  class="style-option"
                  :class="{ 'selected': generateForm.style === style.id }"
                >
                  <input
                    type="radio"
                    v-model="generateForm.style"
                    :value="style.id"
                    hidden
                  />
                  {{ style.name }}
                </label>
              </div>
            </div>

            <div class="form-group">
              <label>选择模板</label>
              <div class="template-options" v-if="!templatesLoading && templates.length">
                <label
                  v-for="tpl in templates"
                  :key="tpl.id"
                  class="template-option"
                  :class="{ selected: generateForm.templateId === tpl.id }"
                >
                  <input
                    type="radio"
                    :value="tpl.id"
                    v-model="generateForm.templateId"
                    hidden
                  />
                  <div class="template-thumb">
                    <img
                      v-if="tpl.previewImage || tpl.preview_image"
                      :src="tpl.previewImage || tpl.preview_image"
                      :alt="tpl.name"
                      loading="lazy"
                    >
                    <div v-else class="template-thumb-fallback">无预览</div>
                  </div>
                  <div class="template-option-info">
                    <div class="template-title-row">
                      <strong>{{ tpl.name }}</strong>
                      <span class="template-badge" v-if="tpl.localDownloadUrl">内置模板</span>
                    </div>
                    <small>{{ tpl.provider }}</small>
                    <p>{{ tpl.description }}</p>
                    <div class="template-option-tags" v-if="tpl.tags?.length">
                      <span v-for="tag in tpl.tags" :key="tag">#{{ tag }}</span>
                    </div>
                  </div>
                </label>
              </div>
              <div v-else class="templates-empty">模板列表加载中...</div>
            </div>

            <div class="form-group">
              <label>生成选项</label>
              <div class="generate-options">
                <label class="option-checkbox">
                  <input type="checkbox" v-model="generateForm.includeImages" />
                  包含图片
                </label>
                <label class="option-checkbox">
                  <input type="checkbox" v-model="generateForm.includeCharts" />
                  包含图表
                </label>
                <label class="option-checkbox">
                  <input type="checkbox" v-model="generateForm.includeNotes" />
                  包含演讲备注
                </label>
              </div>
            </div>

            <div class="form-group">
              <label>选择模型</label>
              <div class="model-options" v-if="!modelsLoading && models.length">
                <label
                  v-for="model in models"
                  :key="model.id"
                  class="model-option"
                  :class="{ selected: generateForm.modelId === model.id }"
                >
                  <input
                    type="radio"
                    :value="model.id"
                    v-model="generateForm.modelId"
                    hidden
                  />
                  <div class="model-title">{{ model.name }}</div>
                  <p class="model-desc">{{ model.description }}</p>
                  <small>{{ model.provider }} · {{ model.locale }}</small>
                </label>
              </div>
              <div v-else class="templates-empty">模型列表加载中...</div>
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <button class="modal-btn secondary" @click="showGenerateModal = false">取消</button>
          <button class="modal-btn primary" @click="handleGenerate" :disabled="generating">
            <span class="btn-content">
              <el-icon class="btn-icon"><MagicStick /></el-icon>
              <span v-if="generating">生成中...</span>
              <span v-else>开始生成</span>
            </span>
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onBeforeUnmount, watch , watchEffect } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useStore } from 'vuex'
import { ElMessage, ElMessageBox } from 'element-plus'
import { MagicStick, Download, Delete, EditPen, Search } from '@element-plus/icons-vue'
import templatesAPI from '@/api/templates'
import pptAPI from '@/api/ppt'

const route = useRoute()
const router = useRouter()
const store = useStore()

const models = computed(() => store.getters.models || [])
const modelsLoading = computed(() => store.getters.modelsLoading)
const selectedModel = computed(() => store.getters.selectedModel)

const showGenerateModal = ref(false)
const generating = ref(false)
const templateQuery = ref('')
const historyQuery = ref('')
const templateSearchResults = ref([])
const templateSearchLoading = ref(false)
let templateSearchTimer = null
let templateSearchVersion = 0
const historySearchResults = ref([])
const historySearchLoading = ref(false)
let historySearchTimer = null
let historySearchVersion = 0
const historyPage = ref(1)
const historyPageSize = ref(6)
const futurePlan = reactive({
  dataset: localStorage.getItem('futureDataset') || '',
  notes: localStorage.getItem('futureNotes') || ''
})
const previewFileUrl = ref('')
const previewMode = ref('online')
const previewSlides = ref([])
const previewIndex = ref(0)
const previewRequestId = ref(0)
const previewDownloadUrl = ref('')

const resolveAbsoluteUrl = (url) => {
  if (!url) return ''
  if (url.startsWith('http')) return url
  const base = import.meta.env.VITE_API_URL || '/api'
  if (base.startsWith('http')) {
    return new URL(url, base).toString()
  }
  return new URL(url, window.location.origin).toString()
}

const buildPreviewFileUrl = (downloadUrl) => {
  if (!downloadUrl) return ''
  try {
    if (downloadUrl.startsWith('http')) {
      return downloadUrl
    }
    const apiBase = import.meta.env.VITE_API_URL || '/api'
    const base = apiBase.startsWith('http') ? apiBase : window.location.origin
    const url = new URL(downloadUrl, base)
    const token = store.state.token
    if (token) {
      url.searchParams.set('token', token)
    }
    url.searchParams.set('inline', '1')
    url.searchParams.set('ngrok-skip-browser-warning', '1')
    return url.toString()
  } catch (error) {
    return downloadUrl
  }
}

const previewEmbedUrl = computed(() => {
  const absUrl = resolveAbsoluteUrl(previewFileUrl.value)
  if (!absUrl) return ''
  return `https://view.officeapps.live.com/op/embed.aspx?src=${encodeURIComponent(absUrl)}`
})

watchEffect(() => {
  console.log('Preview Embed URL:', previewEmbedUrl.value)
})

watchEffect(() => {
  console.log('Preview File URL:', previewFileUrl.value, 'Download URL:', previewDownloadUrl.value)
})

const hasLocalPreview = computed(() => Array.isArray(previewSlides.value) && previewSlides.value.length > 0)
const previewSlideCount = computed(() => previewSlides.value.length)
const currentPreviewSlide = computed(() => {
  if (!hasLocalPreview.value) return null
  const index = Math.min(Math.max(previewIndex.value, 0), previewSlides.value.length - 1)
  return previewSlides.value[index]
})
const previewLayoutClass = computed(() => {
  const slide = currentPreviewSlide.value
  if (!slide) return 'layout-default'
  const rawType = slide.layout?.type || slide.layoutHint || 'default'
  const normalized = String(rawType)
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
  return `layout-${normalized || 'default'}`
})
const currentPreviewImages = computed(() => currentPreviewSlide.value?.imageUrls || [])
const currentPreviewPrompts = computed(() => currentPreviewSlide.value?.imagePrompts || [])
const currentPreviewNotes = computed(() => currentPreviewSlide.value?.notes || '')
const currentPreviewSuggestions = computed(() => currentPreviewSlide.value?.suggestions || [])
const currentPreviewBullets = computed(() => currentPreviewSlide.value?.bullets || [])
const currentPreviewRawText = computed(() => currentPreviewSlide.value?.rawText || '')
const previewFallbackImage = computed(() => {
  const template = selectedTemplate.value
  return template?.previewImage || template?.preview_image || ''
})
const hasPreviewMedia = computed(() => {
  return currentPreviewImages.value.length > 0 ||
    currentPreviewPrompts.value.length > 0 ||
    Boolean(previewFallbackImage.value)
})
const canUseOnlinePreview = computed(() => Boolean(previewEmbedUrl.value))
const canUseLocalPreview = computed(() => hasLocalPreview.value)

const setPreviewMode = (mode) => {
  if (mode === 'online' && !canUseOnlinePreview.value) {
    return
  }
  if (mode === 'local' && !canUseLocalPreview.value) {
    return
  }
  previewMode.value = mode
}

const normalizePreviewSlide = (slide) => {
  if (!slide || typeof slide !== 'object') return null
  return {
    title: slide.title || '',
    bullets: Array.isArray(slide.bullets) ? slide.bullets : [],
    rawText: slide.rawText || slide.raw_text || '',
    imageUrls: Array.isArray(slide.imageUrls || slide.image_urls) ? (slide.imageUrls || slide.image_urls) : [],
    imagePrompts: Array.isArray(slide.imagePrompts || slide.image_prompts) ? (slide.imagePrompts || slide.image_prompts) : [],
    suggestions: Array.isArray(slide.suggestions) ? slide.suggestions : [],
    notes: slide.notes || '',
    layout: slide.layout || null,
    layoutHint: slide.layoutHint || slide.layout_hint || ''
  }
}

const resolveDownloadUrl = (request) => {
  if (!request) return ''
  if (request.downloadUrl) return request.downloadUrl
  if (request.id) return `/api/ppt/file?id=${request.id}`
  return ''
}

const hydratePreviewFromRequest = async (request, { force = false } = {}) => {
  if (!request?.id) return
  if (!force && previewRequestId.value === request.id && (previewFileUrl.value || previewSlides.value.length)) {
    return
  }
  previewRequestId.value = request.id
  previewDownloadUrl.value = request.downloadUrl || ''
  previewFileUrl.value = buildPreviewFileUrl(resolveDownloadUrl(request))
  previewSlides.value = []
  previewIndex.value = 0
  try {
    const response = await pptAPI.preview(request.id)
    const slides = Array.isArray(response.data?.slides)
      ? response.data.slides
      : Array.isArray(response.data?.preview)
        ? response.data.preview
        : []
    previewSlides.value = slides.map(normalizePreviewSlide).filter(Boolean)
  } catch (error) {
    previewSlides.value = []
  } finally {
    syncPreviewMode()
  }
}

const applyLatestPreview = async (items, { force = false } = {}) => {
  if (!Array.isArray(items) || !items.length) {
    return
  }
  if (!force && (previewFileUrl.value || previewSlides.value.length)) {
    return
  }
  const latest =
    items.find(item => item?.hasFile && item?.downloadUrl) ||
    items.find(item => item?.hasFile) ||
    items[0]
  if (latest) {
    await hydratePreviewFromRequest(latest, { force })
  }
}

const goPreviewPrev = () => {
  if (!hasLocalPreview.value) return
  previewIndex.value = Math.max(0, previewIndex.value - 1)
}

const goPreviewNext = () => {
  if (!hasLocalPreview.value) return
  previewIndex.value = Math.min(previewSlides.value.length - 1, previewIndex.value + 1)
}

const selectPreviewSlide = (index) => {
  if (!hasLocalPreview.value) return
  if (index < 0 || index >= previewSlides.value.length) return
  previewIndex.value = index
}

const syncPreviewMode = () => {
  if (previewMode.value === 'online' && canUseOnlinePreview.value) {
    return
  }
  if (previewMode.value === 'local' && canUseLocalPreview.value) {
    return
  }
  if (canUseOnlinePreview.value) {
    previewMode.value = 'online'
  } else if (canUseLocalPreview.value) {
    previewMode.value = 'local'
  }
}

const menuItems = [
  { id: 'dashboard', text: '仪表板', icon: '📊', path: '/main', description: '系统概览与快速操作' },
  { id: 'generate', text: '智能生成', icon: '🤖', path: '/main/generate', description: 'GenAI智能PPT生成' },
  { id: 'templates', text: '模板中心', icon: '🎨', path: '/main/templates', description: '精选PPT模板库' },
  { id: 'history', text: '历史记录', icon: '📋', path: '/main/history', description: '历史生成记录管理' },
  { id: 'settings', text: '系统设置', icon: '⚙️', path: '/main/settings', description: '个性化系统配置' }
]

const resolveSection = (section) => {
  if (!section) return 'dashboard'
  const exists = menuItems.find(item => item.id === section)
  return exists ? section : 'dashboard'
}

const activeMenu = ref(resolveSection(route.params.section))

watch(
  () => route.params.section,
  (section) => {
    activeMenu.value = resolveSection(section)
  }
)

const generateForm = ref({
  title: '',
  topic: '',
  pages: 10,
  style: 'business',
  includeImages: true,
  includeCharts: true,
  includeNotes: false,
  modelId: '',
  templateId: ''
})

watch(
  selectedModel,
  (modelId) => {
    if (modelId && !generateForm.value.modelId) {
      generateForm.value.modelId = modelId
    }
  },
  { immediate: true }
)

watch(
  () => futurePlan.dataset,
  (value) => {
    localStorage.setItem('futureDataset', value || '')
  }
)

watch(
  () => futurePlan.notes,
  (value) => {
    localStorage.setItem('futureNotes', value || '')
  }
)

const resolveTemplateTheme = (template) => {
  const theme = template?.theme || {}
  return {
    primary: theme.primaryColor || theme.primary_color || '#0f172a',
    secondary: theme.secondaryColor || theme.secondary_color || '#1d4ed8',
    accent: theme.accentColor || theme.accent_color || '#f97316',
    background: theme.backgroundImage || theme.background_image || ''
  }
}

const styles = [
  { id: 'business', name: '商务' },
  { id: 'academic', name: '学术' },
  { id: 'creative', name: '创意' },
  { id: 'minimal', name: '简约' }
]

const currentUser = computed(() => store.getters.currentUser)
const userInitials = computed(() => {
  if (!currentUser.value?.username) return 'U'
  return currentUser.value.username.charAt(0).toUpperCase()
})

const pptHistory = computed(() => store.getters.pptHistory)
const historyLoading = computed(() => store.getters.historyLoading)
const templates = computed(() => store.getters.templates || [])
const templatesLoading = computed(() => store.getters.templatesLoading)
const selectedTemplate = computed(() => {
  return templates.value.find(item => item.id === generateForm.value.templateId) || null
})
const previewCardStyle = computed(() => {
  const template = selectedTemplate.value
  const theme = resolveTemplateTheme(template)
  const themeBg = theme.background
  const fallbackBg = template?.previewImage || template?.preview_image
  const background = themeBg || fallbackBg
  const primary = theme.primary
  const secondary = theme.secondary
  const accent = theme.accent
  const style = {
    '--preview-primary': primary,
    '--preview-secondary': secondary,
    '--preview-accent': accent
  }
  if (background) {
    style.backgroundImage = `linear-gradient(135deg, rgba(15,23,42,0.82), rgba(15,23,42,0.35)), url(${background})`
    style.backgroundSize = 'cover'
    style.backgroundPosition = 'center'
  }
  return style
})

watch(
  templates,
  (list) => {
    if (!generateForm.value.templateId && Array.isArray(list) && list.length) {
      generateForm.value.templateId = list[0].id
    }
  },
  { immediate: true }
)

watch(
  pptHistory,
  (items) => {
    applyLatestPreview(items)
  },
  { immediate: true }
)

const statCards = computed(() => {
  const total = pptHistory.value.length
  const totalPages = pptHistory.value.reduce((acc, item) => acc + (item.pages || 0), 0)
  const averagePages = total ? Math.round(totalPages / total) : 0
  const latestStatus = pptHistory.value.length ? pptHistory.value[0].status : 'N/A'
  return [
    { id: 'generated', icon: '📊', title: '已生成PPT', value: total },
    { id: 'pages', icon: '⏱️', title: '平均页数', value: averagePages },
    { id: 'status', icon: '⭐', title: '最新状态', value: latestStatus },
    { id: 'templates', icon: '🔄', title: '可用模板', value: templates.value.length }
  ]
})

const activeMenuItem = computed(() => menuItems.find(item => item.id === activeMenu.value))

const filteredHistory = computed(() => {
  if (!historyQuery.value) {
    return pptHistory.value
  }
  return historySearchResults.value
})

const historyTotal = computed(() => filteredHistory.value.length)
const historyPageCount = computed(() => Math.max(1, Math.ceil(historyTotal.value / historyPageSize.value)))
const pagedHistory = computed(() => {
  const size = historyPageSize.value
  const start = (historyPage.value - 1) * size
  return filteredHistory.value.slice(start, start + size)
})
const showHistoryPagination = computed(() => historyTotal.value > historyPageSize.value)

const filteredTemplates = computed(() => {
  if (!templateQuery.value) {
    return templates.value
  }
  return templateSearchResults.value
})

const templatesBusy = computed(() => {
  if (templateQuery.value) {
    return templateSearchLoading.value
  }
  return templatesLoading.value
})

const templatesBusyLabel = computed(() => (templateQuery.value ? '模板搜索中...' : '模板加载中...'))

const historyBusy = computed(() => {
  if (historyQuery.value) {
    return historySearchLoading.value
  }
  return historyLoading.value
})

const historyBusyLabel = computed(() => (historyQuery.value ? '历史搜索中...' : '历史记录加载中...'))

const performTemplateSearch = async (query) => {
  const currentVersion = ++templateSearchVersion
  templateSearchLoading.value = true
  try {
    const response = await templatesAPI.list({ q: query })
    if (currentVersion !== templateSearchVersion) {
      return
    }
    templateSearchResults.value = response.data?.items || []
  } catch (error) {
    if (currentVersion !== templateSearchVersion) {
      return
    }
    const fallback = templates.value.filter(item => {
      const fields = [
        item.name || '',
        item.description || '',
        item.provider || '',
        ...(item.tags || [])
      ]
      return fields.some(field => field.toLowerCase().includes(query.toLowerCase()))
    })
    templateSearchResults.value = fallback
    ElMessage.error('模板搜索失败，请稍后重试')
  } finally {
    if (currentVersion === templateSearchVersion) {
      templateSearchLoading.value = false
    }
  }
}

const triggerTemplateSearch = () => {
  const query = templateQuery.value.trim()
  if (templateSearchTimer) {
    clearTimeout(templateSearchTimer)
    templateSearchTimer = null
  }
  if (!query) {
    templateSearchVersion += 1
    templateSearchResults.value = []
    templateSearchLoading.value = false
    return
  }
  performTemplateSearch(query)
}

const performHistorySearch = async (query) => {
  const currentVersion = ++historySearchVersion
  historySearchLoading.value = true
  try {
    const items = await store.dispatch('searchPptHistory', query)
    if (currentVersion !== historySearchVersion) {
      return
    }
    historySearchResults.value = items
  } catch (error) {
    if (currentVersion !== historySearchVersion) {
      return
    }
    const fallback = pptHistory.value.filter(item => {
      const title = item.title?.toLowerCase() || ''
      const topic = item.topic?.toLowerCase() || ''
      return title.includes(query.toLowerCase()) || topic.includes(query.toLowerCase())
    })
    historySearchResults.value = fallback
    ElMessage.error('历史搜索失败，请稍后重试')
  } finally {
    if (currentVersion === historySearchVersion) {
      historySearchLoading.value = false
    }
  }
}

const triggerHistorySearch = () => {
  const query = historyQuery.value.trim()
  if (historySearchTimer) {
    clearTimeout(historySearchTimer)
    historySearchTimer = null
  }
  if (!query) {
    historySearchVersion += 1
    historySearchResults.value = []
    historySearchLoading.value = false
    return
  }
  historyPage.value = 1
  performHistorySearch(query)
}

const handleHistoryPageChange = (page) => {
  historyPage.value = page
}

const handleHistorySizeChange = (size) => {
  historyPageSize.value = size
  historyPage.value = 1
}

watch(
  historyQuery,
  (value) => {
    const query = value.trim()
    historyPage.value = 1
    if (!query) {
      historySearchResults.value = []
      historySearchLoading.value = false
      if (historySearchTimer) {
        clearTimeout(historySearchTimer)
        historySearchTimer = null
      }
      return
    }
    if (historySearchTimer) {
      clearTimeout(historySearchTimer)
    }
    historySearchTimer = setTimeout(() => {
      performHistorySearch(query)
    }, 300)
  }
)

watch(
  [historyTotal, historyPageSize],
  () => {
    if (!historyTotal.value) {
      historyPage.value = 1
      return
    }
    const maxPage = historyPageCount.value
    if (historyPage.value > maxPage) {
      historyPage.value = maxPage
    }
  }
)

watch(
  templateQuery,
  (value) => {
    const query = value.trim()
    if (!query) {
      templateSearchResults.value = []
      templateSearchLoading.value = false
      if (templateSearchTimer) {
        clearTimeout(templateSearchTimer)
        templateSearchTimer = null
      }
      return
    }
    if (templateSearchTimer) {
      clearTimeout(templateSearchTimer)
    }
    templateSearchTimer = setTimeout(() => {
      performTemplateSearch(query)
    }, 300)
  }
)

onBeforeUnmount(() => {
  if (templateSearchTimer) {
    clearTimeout(templateSearchTimer)
    templateSearchTimer = null
  }
  if (historySearchTimer) {
    clearTimeout(historySearchTimer)
    historySearchTimer = null
  }
})

const formatDate = (value) => {
  if (!value) return '未知'
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) {
    return value
  }
  return date.toLocaleString('zh-CN', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit'
  })
}

const resetGenerateForm = () => {
  const keepTemplateId = generateForm.value.templateId || templates.value[0]?.id || ''
  generateForm.value = {
    title: '',
    topic: '',
    pages: 10,
    style: 'business',
    includeImages: true,
    includeCharts: true,
    includeNotes: false,
    modelId: selectedModel.value || 'qwen-turbo',
    templateId: keepTemplateId
  }
}

const handleGenerate = async () => {
  if (!generateForm.value.title.trim() || !generateForm.value.topic.trim()) {
    ElMessage.warning('请填写标题和主题描述')
    return
  }
  if (!generateForm.value.templateId) {
    ElMessage.warning('请选择模板样式')
    return
  }

  generating.value = true
  previewFileUrl.value = ''
  previewSlides.value = []
  previewIndex.value = 0
  try {
    const payload = {
      title: generateForm.value.title.trim(),
      topic: generateForm.value.topic.trim(),
      pages: generateForm.value.pages,
      style: generateForm.value.style,
      includeImages: generateForm.value.includeImages,
      includeCharts: generateForm.value.includeCharts,
      includeNotes: generateForm.value.includeNotes,
      modelId: generateForm.value.modelId || selectedModel.value,
      templateId: generateForm.value.templateId
    }
    const result = await store.dispatch('createPptRequest', payload)
    if (!result?.request) {
      throw new Error('生成请求失败')
    }
    previewDownloadUrl.value = result.request?.downloadUrl || ''
    previewFileUrl.value = buildPreviewFileUrl(resolveDownloadUrl(result.request))
    previewSlides.value = Array.isArray(result.preview)
      ? result.preview.map(normalizePreviewSlide).filter(Boolean)
      : []
    previewIndex.value = 0
    previewRequestId.value = result.request?.id || 0
    if (!previewSlides.value.length && result.request?.id) {
      await hydratePreviewFromRequest(result.request, { force: true })
    } else {
      syncPreviewMode()
    }
    await store.dispatch('fetchPptHistory')
    resetGenerateForm()
    showGenerateModal.value = false
    activeMenu.value = 'generate'
  } catch (error) {
    console.error('生成失败:', error)
    const message = error.response?.data?.message || error.message || '生成失败，请重试'
    ElMessage.error(message)
  } finally {
    generating.value = false
  }
}

const editPPT = (item) => {
  ElMessage.info(`编辑功能即将上线：${item.title}`)
}

const downloadPPT = (item) => {
  ElMessage.info(`下载功能即将上线：${item.title}`)
}

const deleteHistory = async (item) => {
  if (!item?.id) {
    return
  }
  try {
    await ElMessageBox.confirm(
      `确定删除《${item.title || '未命名PPT'}》吗？`,
      '删除确认',
      {
        confirmButtonText: '删除',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )
    await store.dispatch('deletePptRequest', item.id)
    if (historyQuery.value) {
      historySearchResults.value = historySearchResults.value.filter(entry => entry.id !== item.id)
    }
    ElMessage.success('删除成功')
  } catch (error) {
    if (error === 'cancel' || error === 'close' || error?.message === 'cancel') {
      return
    }
    console.error('删除失败:', error)
    const message = error.response?.data?.message || error.message || '删除失败，请稍后重试'
    ElMessage.error(message)
  }
}

const useTemplate = (template) => {
  generateForm.value.title = template.name || '自定义PPT'
  if (template.description) {
    generateForm.value.topic = template.description
  }
  generateForm.value.modelId = selectedModel.value || generateForm.value.modelId
  if (template.id) {
    generateForm.value.templateId = template.id
  }
  showGenerateModal.value = true
  if (route.params.section !== 'generate') {
    router.push('/main/generate')
  }
}

const handleModelChange = (modelId) => {
  store.dispatch('updateDefaultModel', modelId)
  generateForm.value.modelId = modelId
}

const handleLogout = async () => {
  try {
    await store.dispatch('logout')
  } catch (error) {
    console.error('退出登录失败:', error)
  } finally {
    previewFileUrl.value = ''
    previewSlides.value = []
    previewIndex.value = 0
    previewMode.value = 'online'
    previewRequestId.value = 0
    previewDownloadUrl.value = ''
    router.push('/login')
  }
}

const ensureSession = async () => {
  try {
    if (!store.state.user) {
      await store.dispatch('fetchCurrentUser')
    }
  } catch (error) {
    if (error?.response?.status === 401) {
      router.push('/login')
      return
    }
    console.error('加载用户数据失败:', error)
    ElMessage.warning('用户信息加载失败，将继续尝试加载数据')
  }

  const results = await Promise.allSettled([
    store.dispatch('fetchPptHistory'),
    store.dispatch('fetchTemplates'),
    store.dispatch('fetchModels')
  ])
  results.forEach((result) => {
    if (result.status === 'rejected') {
      const status = result.reason?.response?.status
      if (status === 401) {
        router.push('/login')
      } else {
        console.error('加载数据失败:', result.reason)
      }
    }
  })
  const historyResult = results[0]
  if (historyResult?.status === 'fulfilled') {
    await applyLatestPreview(historyResult.value, { force: true })
  }
}

onMounted(() => {
  if (!store.state.isAuthenticated) {
    router.push('/login')
    return
  }
  ensureSession()
})
</script>

<style scoped>
.main-container {
  display: flex;
  min-height: 100vh;
  background: #f8fafc;
}

.sidebar {
  width: 280px;
  background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%);
  color: white;
  display: flex;
  flex-direction: column;
  box-shadow: 5px 0 15px rgba(0, 0, 0, 0.1);
}

.sidebar-header {
  padding: 30px 20px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.sidebar-header h2 {
  font-size: 1.5rem;
  margin-bottom: 20px;
  font-weight: 600;
}

.user-info {
  display: flex;
  align-items: center;
  gap: 12px;
}

.avatar {
  width: 50px;
  height: 50px;
  background: linear-gradient(135deg, #4f46e5 0%, #7c3aed 100%);
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.2rem;
  font-weight: bold;
}

.user-details {
  flex: 1;
}

.username {
  font-weight: 600;
  font-size: 1rem;
}

.user-email {
  font-size: 0.875rem;
  opacity: 0.8;
}

.sidebar-nav {
  flex: 1;
  padding: 20px 0;
}

.nav-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 15px 25px;
  color: rgba(255, 255, 255, 0.8);
  text-decoration: none;
  transition: all 0.3s;
  border-left: 4px solid transparent;
}

.nav-item:hover {
  background: rgba(255, 255, 255, 0.1);
  color: white;
  border-left-color: #4f46e5;
}

.nav-item.active {
  background: rgba(79, 70, 229, 0.2);
  color: white;
  border-left-color: #4f46e5;
}

.nav-icon {
  font-size: 1.2rem;
}

.nav-text {
  font-size: 1rem;
  font-weight: 500;
}

.sidebar-footer {
  padding: 20px;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
}

.logout-btn {
  display: flex;
  align-items: center;
  gap: 10px;
  width: 100%;
  padding: 12px;
  background: rgba(239, 68, 68, 0.1);
  color: #ef4444;
  border: none;
  border-radius: 10px;
  cursor: pointer;
  transition: all 0.3s;
}

.logout-btn:hover {
  background: rgba(239, 68, 68, 0.2);
}

.main-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.main-header {
  background: white;
  padding: 20px 30px;
  border-bottom: 1px solid #e5e7eb;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.header-left h1 {
  font-size: 1.8rem;
  color: #1e293b;
  margin-bottom: 5px;
}

.page-description {
  color: #64748b;
  font-size: 0.95rem;
}

.generate-btn {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px 24px;
  background: linear-gradient(135deg, #4f46e5 0%, #7c3aed 100%);
  color: white;
  border: none;
  border-radius: 10px;
  font-size: 1rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.3s;
}

.generate-btn:hover {
  transform: translateY(-2px);
  box-shadow: 0 10px 20px rgba(79, 70, 229, 0.3);
}

.content-area {
  flex: 1;
  padding: 30px;
  overflow-y: auto;
}

.dashboard {
  display: flex;
  flex-direction: column;
  gap: 30px;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 20px;
}

.stat-card {
  background: white;
  border-radius: 15px;
  padding: 25px;
  box-shadow: 0 5px 15px rgba(0, 0, 0, 0.05);
  display: flex;
  align-items: center;
  gap: 20px;
  transition: transform 0.3s;
}

.stat-card:hover {
  transform: translateY(-5px);
}

.stat-icon {
  font-size: 3rem;
  opacity: 0.8;
}

.stat-content h3 {
  font-size: 1rem;
  color: #64748b;
  margin-bottom: 5px;
}

.stat-number {
  font-size: 2.5rem;
  font-weight: bold;
  color: #1e293b;
}

.features-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 20px;
}

.feature-card {
  background: white;
  border-radius: 15px;
  padding: 25px;
  box-shadow: 0 5px 15px rgba(0, 0, 0, 0.05);
  text-align: center;
  transition: transform 0.3s;
}

.feature-card:hover {
  transform: translateY(-5px);
}

.feature-icon {
  font-size: 3rem;
  margin-bottom: 15px;
}

.feature-card h4 {
  font-size: 1.2rem;
  margin-bottom: 10px;
  color: #1e293b;
}

.feature-card p {
  color: #64748b;
  margin-bottom: 20px;
  line-height: 1.5;
}

.feature-action {
  padding: 10px 20px;
  background: #f1f5f9;
  border: none;
  border-radius: 8px;
  color: #4f46e5;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.3s;
}

.feature-action:hover {
  background: #e0e7ff;
}

.history-section {
  background: white;
  border-radius: 15px;
  padding: 25px;
}

.section-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 30px;
}

.search-box input {
  padding: 10px 15px;
  border: 2px solid #e5e7eb;
  border-radius: 8px;
  width: 300px;
}

.template-search-box {
  display: flex;
  align-items: center;
  gap: 10px;
}

.template-search-box :deep(.el-input__wrapper) {
  border: 2px solid #e5e7eb;
  border-radius: 8px;
  box-shadow: none;
  padding: 0 12px;
  width: 300px;
}

.template-search-box :deep(.el-input__wrapper.is-focus) {
  border-color: #4f46e5;
}

.template-search-box :deep(.el-input__inner) {
  height: 42px;
  font-size: 1rem;
}

.template-search-box :deep(.el-input__prefix-inner svg) {
  width: 18px;
  height: 18px;
}

.search-icon-btn {
  width: 42px;
  height: 42px;
}

.history-search-box {
  display: flex;
  align-items: center;
  gap: 10px;
}

.history-search-box :deep(.el-input__wrapper) {
  border: 2px solid #e5e7eb;
  border-radius: 8px;
  box-shadow: none;
  padding: 0 12px;
  width: 300px;
}

.history-search-box :deep(.el-input__wrapper.is-focus) {
  border-color: #4f46e5;
}

.history-search-box :deep(.el-input__inner) {
  height: 42px;
  font-size: 1rem;
}

.history-search-box :deep(.el-input__prefix-inner svg) {
  width: 18px;
  height: 18px;
}

.history-list {
  display: flex;
  flex-direction: column;
  gap: 15px;
}

.history-pagination {
  display: flex;
  justify-content: center;
  margin-top: 20px;
}

.history-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  transition: all 0.3s;
}

.history-item:hover {
  border-color: #4f46e5;
  box-shadow: 0 5px 15px rgba(79, 70, 229, 0.1);
}

.history-preview {
  display: flex;
  align-items: center;
  gap: 15px;
}

.preview-icon {
  font-size: 2rem;
}

.preview-content h4 {
  font-size: 1.1rem;
  margin-bottom: 5px;
  color: #1e293b;
}

.preview-content p {
  color: #64748b;
  margin-bottom: 10px;
}

.history-meta {
  display: flex;
  gap: 20px;
  font-size: 0.875rem;
  color: #94a3b8;
  flex-wrap: wrap;
}

.history-actions {
  display: flex;
  gap: 10px;
}

.history-empty {
  padding: 40px 20px;
  text-align: center;
  border: 2px dashed #e2e8f0;
  border-radius: 12px;
  color: #94a3b8;
  font-size: 1rem;
}

.templates-empty {
  padding: 40px 20px;
  text-align: center;
  border: 2px dashed #e2e8f0;
  border-radius: 12px;
  color: #94a3b8;
  font-size: 1rem;
}

.generate-section {
  background: white;
  border-radius: 16px;
  padding: 24px;
}

.generate-panel h2 {
  margin-bottom: 8px;
}

.template-hint {
  margin-top: 8px;
  font-size: 0.9rem;
  color: #475569;
  display: flex;
  align-items: center;
  gap: 16px;
}

.template-download-link {
  font-size: 0.85rem;
  color: #2563eb;
  text-decoration: underline;
}

.preview-card {
  margin-top: 16px;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  padding: 20px;
  background: #f8fafc;
  --preview-primary: #0f172a;
  --preview-secondary: #475569;
  --preview-accent: #6366f1;
}

.preview-empty {
  margin-top: 16px;
  padding: 24px;
  text-align: center;
  border: 2px dashed #e2e8f0;
  border-radius: 12px;
  color: #94a3b8;
  background: #f8fafc;
}

.preview-card.preview-has-bg {
  color: #f8fafc;
  border-color: rgba(255, 255, 255, 0.25);
  box-shadow: 0 25px 60px rgba(15, 23, 42, 0.4);
}

.preview-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.preview-label {
  font-size: 0.9rem;
  font-weight: 600;
  color: var(--preview-accent);
}

.preview-actions {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.preview-mode {
  display: flex;
  gap: 8px;
}

.preview-toggle {
  padding: 6px 12px;
  border-radius: 999px;
  border: 1px solid #cbd5f5;
  background: #ffffff;
  color: #334155;
  font-size: 0.85rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s ease;
}

.preview-toggle.active {
  background: #eef2ff;
  border-color: #4f46e5;
  color: #312e81;
}

.preview-toggle:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.preview-counter {
  font-size: 0.9rem;
  color: var(--preview-secondary);
}

.preview-body h3 {
  margin-bottom: 10px;
  color: var(--preview-primary);
}

.preview-card.preview-has-bg .preview-body h3 {
  color: #f8fafc;
}

.preview-card ul {
  margin-top: 12px;
  padding-left: 20px;
}

.preview-card li {
  margin-bottom: 6px;
  color: var(--preview-primary);
}

.preview-raw {
  margin-top: 12px;
  color: var(--preview-primary);
  line-height: 1.6;
}

.preview-placeholder {
  margin-top: 12px;
  color: #94a3b8;
  font-style: italic;
}

.preview-embed {
  margin-top: 8px;
}

.preview-iframe {
  width: 100%;
  height: 520px;
  border: none;
  border-radius: 10px;
  background: #ffffff;
}

.btn-content {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.btn-icon {
  font-size: 1rem;
  line-height: 1;
}

.preview-card.preview-has-bg .preview-placeholder {
  color: rgba(248, 250, 252, 0.8);
}

.image-prompts {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
  margin-top: 12px;
}

.image-prompts span {
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 0.75rem;
  background: rgba(99, 102, 241, 0.12);
  border: 1px solid var(--preview-accent);
  color: var(--preview-accent);
}

.preview-card.preview-has-bg .image-prompts span {
  background: rgba(255, 255, 255, 0.16);
  color: #e0e7ff;
  border-color: rgba(255, 255, 255, 0.4);
}

.layout-grid {
  display: grid;
  grid-template-columns: 3fr 2fr;
  gap: 20px;
  align-items: stretch;
}

.layout-default .layout-grid,
.layout-cover .layout-grid {
  grid-template-columns: 1fr;
}

.layout-two-column .layout-grid,
.layout-story .layout-grid,
.layout-map .layout-grid,
.layout-stats .layout-grid {
  grid-template-columns: 3fr 2fr;
}

.layout-text {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.layout-media {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.layout-media .preview-images {
  margin-top: 0;
}

.layout-cover .layout-text h3 {
  font-size: 2rem;
  letter-spacing: 1px;
  text-transform: uppercase;
}

.preview-images {
  margin-top: 16px;
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.preview-images img {
  width: 200px;
  height: 120px;
  object-fit: cover;
  border-radius: 10px;
  box-shadow: 0 5px 15px rgba(15, 23, 42, 0.1);
  border: 1px solid #e2e8f0;
}

.preview-template-fallback {
  margin-top: 16px;
}

.preview-template-fallback img {
  width: 240px;
  border-radius: 12px;
  border: 1px solid #e2e8f0;
}

.preview-controls {
  display: flex;
  gap: 12px;
  margin-top: 16px;
}

.preview-nav {
  flex: 1;
  padding: 10px 16px;
  border: 1px solid #cbd5f5;
  border-radius: 10px;
  background: white;
  font-weight: 600;
  color: #334155;
  cursor: pointer;
  transition: background 0.2s, color 0.2s;
}

.preview-nav:hover:not(:disabled) {
  background: #eef2ff;
}

.preview-nav:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.preview-thumbnails {
  margin-top: 16px;
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.preview-thumb {
  display: flex;
  align-items: center;
  gap: 6px;
  border: 1px solid #e2e8f0;
  border-radius: 999px;
  padding: 6px 12px;
  background: white;
  font-size: 0.85rem;
  color: #475569;
  cursor: pointer;
  transition: all 0.2s ease;
}

.preview-thumb .thumb-index {
  font-weight: 600;
  color: #4f46e5;
}

.preview-thumb.active {
  border-color: #4f46e5;
  background: #eef2ff;
  color: #312e81;
}

.preview-thumb.active .thumb-index {
  color: #312e81;
}

.settings-section {
  background: white;
  border-radius: 16px;
  padding: 30px;
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.settings-card {
  background: #fff;
}

.settings-card h2 {
  margin-bottom: 8px;
}

.settings-desc {
  color: #64748b;
  margin-bottom: 20px;
}

.settings-block {
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  padding: 20px;
  margin-bottom: 16px;
}

.settings-models {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-top: 12px;
}

.settings-model {
  display: flex;
  gap: 12px;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 12px;
  cursor: pointer;
}

.settings-model.selected {
  border-color: #4f46e5;
  background: #f3f4ff;
}

.settings-model input {
  margin-top: 6px;
}

.model-texts strong {
  display: block;
  font-size: 1rem;
  color: #111827;
}

.model-texts small {
  display: block;
  color: #6b7280;
}

.model-texts p {
  margin: 6px 0;
  color: #475569;
}

.model-tags span {
  margin-right: 12px;
  font-size: 0.85rem;
  color: #4f46e5;
}

.plan-fields {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin: 12px 0;
}

.plan-fields input,
.plan-fields textarea {
  width: 100%;
  margin-top: 6px;
  padding: 10px 12px;
  border: 1px solid #cbd5f5;
  border-radius: 8px;
}

.templates-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 25px;
}

.template-card {
  background: white;
  border-radius: 15px;
  overflow: hidden;
  box-shadow: 0 5px 15px rgba(0, 0, 0, 0.05);
  transition: transform 0.3s;
}

.template-card:hover {
  transform: translateY(-5px);
}

.template-preview {
  height: 200px;
  background: #f1f5f9;
  display: flex;
  align-items: center;
  justify-content: center;
}

.template-preview img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  border-radius: 8px;
}

.default-preview {
  width: 80%;
}

.preview-slides {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.preview-slides .slide {
  height: 30px;
  background: white;
  border-radius: 5px;
  box-shadow: 0 2px 5px rgba(0, 0, 0, 0.1);
}

.template-info {
  padding: 20px;
}

.template-title-row {
  display: flex;
  align-items: center;
  gap: 10px;
}

.template-info h4 {
  font-size: 1.1rem;
  margin-bottom: 10px;
  color: #1e293b;
}

.template-badge {
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 0.75rem;
  background: #e0f2fe;
  color: #0369a1;
  border: 1px solid #7dd3fc;
  white-space: nowrap;
}

.template-desc {
  color: #475569;
  line-height: 1.5;
  margin-bottom: 12px;
}

.template-meta {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 0.9rem;
  color: #64748b;
  margin-bottom: 10px;
}

.template-meta a {
  color: #4f46e5;
}

.template-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 15px;
}

.template-tag {
  background: #eef2ff;
  color: #4338ca;
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 0.8rem;
}

.template-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.use-template-btn {
  flex: 1;
  min-width: 120px;
  padding: 10px;
  background: #4f46e5;
  border: none;
  border-radius: 8px;
  color: #fff;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.3s;
  text-align: center;
}

.use-template-btn:hover {
  opacity: 0.9;
}

.use-template-btn.secondary {
  background: #f8fafc;
  color: #1e293b;
  border: 1px solid #e5e7eb;
}

.use-template-btn.outline {
  background: transparent;
  color: #2563eb;
  border: 1px dashed #93c5fd;
}

.use-template-btn.secondary:hover {
  background: #e2e8f0;
}

.use-template-btn.outline:hover {
  background: rgba(37, 99, 235, 0.08);
}

.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.modal-content {
  background: white;
  border-radius: 20px;
  width: 90%;
  max-width: 800px;
  max-height: 90vh;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.modal-header {
  padding: 25px 30px;
  border-bottom: 1px solid #e5e7eb;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.modal-header h2 {
  font-size: 1.5rem;
  color: #1e293b;
}

.modal-close {
  background: none;
  border: none;
  font-size: 2rem;
  cursor: pointer;
  color: #94a3b8;
  transition: color 0.3s;
}

.modal-close:hover {
  color: #ef4444;
}

.modal-body {
  flex: 1;
  padding: 30px;
  overflow-y: auto;
}

.generate-form {
  display: flex;
  flex-direction: column;
  gap: 25px;
}

.form-group label {
  display: block;
  margin-bottom: 8px;
  font-weight: 500;
  color: #1e293b;
}

.form-group input,
.form-group textarea {
  width: 100%;
  padding: 12px 16px;
  border: 2px solid #e5e7eb;
  border-radius: 10px;
  font-size: 1rem;
  transition: border-color 0.3s;
}

.form-group input:focus,
.form-group textarea:focus {
  outline: none;
  border-color: #4f46e5;
}

.style-options {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.style-option {
  padding: 10px 20px;
  border: 2px solid #e5e7eb;
  border-radius: 10px;
  cursor: pointer;
  transition: all 0.3s;
}

.style-option:hover {
  border-color: #4f46e5;
}

.style-option.selected {
  background: #e0e7ff;
  border-color: #4f46e5;
  color: #4f46e5;
  font-weight: 500;
}

.template-options {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.template-option {
  display: flex;
  gap: 16px;
  padding: 12px;
  border: 2px solid #e2e8f0;
  border-radius: 14px;
  cursor: pointer;
  transition: border-color 0.2s, background 0.2s;
}

.template-option.selected {
  border-color: #4f46e5;
  background: #f5f3ff;
}

.template-thumb {
  width: 140px;
  height: 90px;
  border-radius: 10px;
  overflow: hidden;
  background: #e2e8f0;
  display: flex;
  align-items: center;
  justify-content: center;
}

.template-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.template-thumb-fallback {
  font-size: 0.85rem;
  color: #475569;
}

.template-option-info strong {
  display: block;
  font-size: 1rem;
  color: #0f172a;
}

.template-option-info small {
  display: block;
  color: #64748b;
  margin-bottom: 8px;
}

.template-option-info p {
  margin-bottom: 8px;
  color: #475569;
  font-size: 0.9rem;
}

.template-option-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.template-option-tags span {
  background: #eef2ff;
  color: #4338ca;
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 0.75rem;
}

.generate-options {
  display: flex;
  gap: 20px;
}

.option-checkbox {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
}

.model-options {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.model-option {
  border: 2px solid #e2e8f0;
  border-radius: 12px;
  padding: 12px 16px;
  cursor: pointer;
  transition: border-color 0.2s, background 0.2s;
}

.model-option.selected {
  border-color: #4f46e5;
  background: #f5f3ff;
}

.model-title {
  font-weight: 600;
  color: #111827;
}

.model-desc {
  margin: 6px 0;
  color: #475569;
  font-size: 0.9rem;
}

.modal-footer {
  padding: 20px 30px;
  border-top: 1px solid #e5e7eb;
  display: flex;
  justify-content: flex-end;
  gap: 15px;
}

.modal-btn {
  padding: 12px 24px;
  border: none;
  border-radius: 10px;
  font-size: 1rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.3s;
}

.modal-btn.secondary {
  background: #f1f5f9;
  color: #64748b;
}

.modal-btn.secondary:hover {
  background: #e2e8f0;
}

.modal-btn.primary {
  background: linear-gradient(135deg, #4f46e5 0%, #7c3aed 100%);
  color: white;
}

.modal-btn.primary:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 10px 20px rgba(79, 70, 229, 0.3);
}

.modal-btn.primary:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

@media (max-width: 1200px) {
  .stats-grid,
  .features-grid,
  .templates-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 992px) {
  .sidebar {
    width: 80px;
  }

  .sidebar-header h2,
  .nav-text,
  .user-details,
  .logout-btn span:last-child {
    display: none;
  }

  .avatar {
    width: 40px;
    height: 40px;
    font-size: 1rem;
  }

  .nav-item {
    justify-content: center;
    padding: 15px;
  }

  .logout-btn {
    justify-content: center;
  }
}

@media (max-width: 768px) {
  .stats-grid,
  .features-grid,
  .templates-grid {
    grid-template-columns: 1fr;
  }

  .main-container {
    flex-direction: column;
  }

  .sidebar {
    width: 100%;
    height: 60px;
    flex-direction: row;
    justify-content: space-between;
    padding: 0 20px;
  }

  .sidebar-header,
  .sidebar-footer {
    display: none;
  }

  .sidebar-nav {
    display: flex;
    padding: 0;
  }

  .nav-item {
    flex: 1;
    flex-direction: column;
    padding: 10px;
  }

  .main-header {
    flex-direction: column;
    gap: 15px;
    text-align: center;
  }

  .modal-content {
    width: 95%;
    margin: 10px;
  }
}
</style>
