import { createStore } from 'vuex'
import authAPI, { setAuthToken } from '@/api/auth'
import pptAPI, { watchPptProgress } from '@/api/ppt'
import templatesAPI from '@/api/templates'
import modelsAPI from '@/api/models'
import adminAPI from '@/api/admin'
import assistantModule from './modules/assistant'

const savedToken = localStorage.getItem('token') || sessionStorage.getItem('token')
const savedModel = localStorage.getItem('defaultModel') || 'qwen-turbo'
if (savedToken) {
  setAuthToken(savedToken)
}

const normalizeRequest = (item = {}) => {
  const id = item.id ?? 0
  const hasFile = Boolean(item.hasFile ?? item.has_file)
  const downloadUrl = item.downloadUrl ?? item.download_url ?? (hasFile && id ? `/api/ppt/file?id=${id}` : '')
  const downloadUrlPdf = item.downloadUrlPdf ?? item.download_url_pdf ?? (hasFile && id ? `/api/ppt/file?id=${id}&format=pdf` : '')
  return {
    id,
    userId: item.user_id ?? item.userId ?? 0,
    userName: item.userName ?? item.username ?? '',
    userEmail: item.userEmail ?? item.email ?? '',
    title: item.title ?? '',
    topic: item.topic ?? item.description ?? '',
    pages: item.pages ?? 0,
    style: item.style ?? 'business',
    includeImages: typeof item.includeImages === 'boolean' ? item.includeImages : Boolean(item.include_images),
    includeCharts: typeof item.includeCharts === 'boolean' ? item.includeCharts : Boolean(item.include_charts),
    includeNotes: typeof item.includeNotes === 'boolean' ? item.includeNotes : Boolean(item.include_notes),
    status: item.status ?? 'completed',
    templateId: item.templateId ?? item.template_id ?? '',
    templateName: item.templateName ?? item.template_name ?? '',
    createdAt: item.createdAt ?? item.created_at ?? '',
    updatedAt: item.updatedAt ?? item.updated_at ?? '',
    hasFile,
    downloadUrl,
    downloadUrlPdf
  }
}

export default createStore({
  modules: {
    assistant: assistantModule
  },
  state: {
    user: null,
    token: savedToken || null,
    isAuthenticated: !!savedToken,
    pptHistory: [],
    adminHistory: [],
    templates: [],
    models: [],
    selectedModel: savedModel,
    loading: {
      user: false,
      history: false,
      adminHistory: false,
      templates: false,
      models: false
    },
    aiSearch: {
      mode: false,       // 是否处于 AI 检索模式
      loading: false,
      results: [],
      query: '',
      fallback: false
    },
    // AI 助手工具触发的跨组件状态
    prefillGenerateParams: null,  // { topic, pageCount, style }
    openMaterialUpload: false,    // 触发素材页打开上传面板
    materialPreviewId: null,      // 触发素材页打开指定素材的预览弹窗
    loginHint: null,              // 登录页预填用户名
    // AI 助手驱动的 PPT 生成进度（Main.vue -> AiAssistant.vue）
    assistantGenProgress: null    // null | { stage, step, progress(0-100), done, failed, pptId, title }
  },
  mutations: {
    setUser(state, user) {
      state.user = user
    },
    setToken(state, payload) {
      const token = typeof payload === 'object' && payload !== null ? payload.token : payload
      const remember = typeof payload === 'object' && payload !== null ? payload.remember : true
      state.token = token
      state.isAuthenticated = !!token
      if (token) {
        if (remember) {
          localStorage.setItem('token', token)
          sessionStorage.removeItem('token')
        } else {
          sessionStorage.setItem('token', token)
          localStorage.removeItem('token')
        }
        setAuthToken(token)
      } else {
        localStorage.removeItem('token')
        sessionStorage.removeItem('token')
        setAuthToken(null)
      }
    },
    logout(state) {
      state.user = null
      state.token = null
      state.isAuthenticated = false
      state.pptHistory = []
      state.adminHistory = []
      localStorage.removeItem('token')
      sessionStorage.removeItem('token')
      localStorage.removeItem('rememberedUsername')
      localStorage.removeItem('rememberedPassword')
      localStorage.removeItem('rememberMe')
    },
    setPptHistory(state, items) {
      state.pptHistory = items
    },
    setAdminHistory(state, items) {
      state.adminHistory = items
    },
    prependPptRequest(state, request) {
      state.pptHistory = [request, ...state.pptHistory]
    },
    removePptRequest(state, requestId) {
      state.pptHistory = state.pptHistory.filter(item => item.id !== requestId)
    },
    setTemplates(state, templates) {
      state.templates = templates
    },
    setModels(state, models) {
      state.models = models
    },
    setSelectedModel(state, modelId) {
      state.selectedModel = modelId
      localStorage.setItem('defaultModel', modelId)
    },
    setLoading(state, { key, value }) {
      if (Object.prototype.hasOwnProperty.call(state.loading, key)) {
        state.loading[key] = value
      }
    },
    // ── AI 助手工具触发的跨组件状态 ────────────────────
    /** 助手触发生成时预填的参数，Main.vue 的 generate 子页监听此字段 */
    setPrefillGenerateParams(state, params) {
      state.prefillGenerateParams = params || null
    },
    clearPrefillGenerateParams(state) {
      state.prefillGenerateParams = null
    },
    /** 助手触发打开素材上传面板，materials 子页监听此字段 */
    setOpenMaterialUpload(state, value) {
      state.openMaterialUpload = !!value
    },
    /** 助手触发打开指定素材的预览弹窗，materials 子页监听此字段 */
    setMaterialPreview(state, materialId) {
      state.materialPreviewId = materialId || null
    },
    /** 助手触发登录页预填用户名 */
    setLoginHint(state, username) {
      state.loginHint = username || null
    },
    /** Main.vue 向助手面板推送 PPT 生成进度 */
    setAssistantGenProgress(state, progress) {
      state.assistantGenProgress = progress || null
    },
    setAiSearchMode(state, mode) {
      state.aiSearch.mode = mode
      if (!mode) {
        state.aiSearch.results = []
        state.aiSearch.query = ''
        state.aiSearch.fallback = false
      }
    },
    setAiSearchLoading(state, loading) {
      state.aiSearch.loading = loading
    },
    setAiSearchResults(state, { results, query, fallback }) {
      state.aiSearch.results = results
      state.aiSearch.query = query
      state.aiSearch.fallback = fallback
    }
  },
  actions: {
    async bootstrapSession({ state, commit, dispatch }) {
      const localToken = localStorage.getItem('token')
      const sessionToken = sessionStorage.getItem('token')
      if (!state.token && (localToken || sessionToken)) {
        commit('setToken', localToken ? { token: localToken, remember: true } : { token: sessionToken, remember: false })
      }
      if (!state.token) {
        return
      }
      try {
        if (!state.user) {
          await dispatch('fetchCurrentUser')
        }
        const tasks = [dispatch('fetchPptHistory'), dispatch('fetchTemplates'), dispatch('fetchModels')]
        if (state.user?.isAdmin) {
          tasks.push(dispatch('fetchAdminHistory'))
        }
        await Promise.all(tasks)
      } catch (error) {
        commit('logout')
        throw error
      }
    },
    async fetchCurrentUser({ commit, state }) {
      if (!state.token) {
        return null
      }
      commit('setLoading', { key: 'user', value: true })
      try {
        const response = await authAPI.getUserInfo()
        commit('setUser', response.data.user)
        return response.data.user
      } finally {
        commit('setLoading', { key: 'user', value: false })
      }
    },
    async fetchPptHistory({ commit, state }) {
      if (!state.token) {
        commit('setPptHistory', [])
        return []
      }
      commit('setLoading', { key: 'history', value: true })
      try {
        const response = await pptAPI.history()
        const items = (response.data?.items || []).map(normalizeRequest)
        commit('setPptHistory', items)
        return items
      } finally {
        commit('setLoading', { key: 'history', value: false })
      }
    },
    async fetchAdminHistory({ commit, state }) {
      if (!state.token || !state.user?.isAdmin) {
        commit('setAdminHistory', [])
        return []
      }
      commit('setLoading', { key: 'adminHistory', value: true })
      try {
        const response = await adminAPI.pptHistory()
        const items = (response.data?.items || []).map(normalizeRequest)
        commit('setAdminHistory', items)
        return items
      } finally {
        commit('setLoading', { key: 'adminHistory', value: false })
      }
    },
    async searchPptHistory({ state }, query) {
      if (!state.token) {
        return []
      }
      const keyword = (query || '').trim()
      if (!keyword) {
        return []
      }
      const response = await pptAPI.history({ q: keyword })
      const items = (response.data?.items || []).map(normalizeRequest)
      const lower = keyword.toLowerCase()
      return items.filter(item => {
        const title = item.title?.toLowerCase() || ''
        const topic = item.topic?.toLowerCase() || ''
        return title.includes(lower) || topic.includes(lower)
      })
    },
    async aiSearchPpt({ commit, state }, { query, topK = 10, enableRerank = true }) {
      if (!state.token || !query.trim()) return []
      commit('setAiSearchLoading', true)
      try {
        const response = await pptAPI.aiSearch(query.trim(), topK, enableRerank)
        const results = response.data?.results || []
        const fallback = response.data?.fallback || false
        commit('setAiSearchResults', { results, query: query.trim(), fallback })
        return results
      } finally {
        commit('setAiSearchLoading', false)
      }
    },
    async searchAdminHistory({ state }, query) {
      if (!state.token || !state.user?.isAdmin) {
        return []
      }
      const keyword = (query || '').trim()
      if (!keyword) {
        return []
      }
      const response = await adminAPI.pptHistory({ q: keyword })
      const items = (response.data?.items || []).map(normalizeRequest)
      const lower = keyword.toLowerCase()
      return items.filter(item => {
        const title = item.title?.toLowerCase() || ''
        const topic = item.topic?.toLowerCase() || ''
        const userName = item.userName?.toLowerCase() || ''
        const userEmail = item.userEmail?.toLowerCase() || ''
        return title.includes(lower) || topic.includes(lower) || userName.includes(lower) || userEmail.includes(lower)
      })
    },
    async createPptRequest({ commit, state, dispatch }, payload) {
      const body = { ...payload }
      const onProgress = typeof payload.onProgress === 'function' ? payload.onProgress : null
      delete body.onProgress

      if (!body.modelId) {
        body.modelId = state.selectedModel || 'qwen-turbo'
      }
      const response = await pptAPI.generate(body)
      const request = response.data?.request
      if (!request) {
        return {}
      }
      const normalized = normalizeRequest(request)
      commit('prependPptRequest', normalized)

      const isAsync = response.status === 202 || normalized.status === 'pending' || normalized.status === 'processing'
      if (!isAsync) {
        return { request: normalized, preview: response.data?.preview || null }
      }

      if (onProgress) onProgress({ progress: 5, stage: '初始化', step: '正在提交生成请求...' })

      // ── P4.1：优先使用 SSE 流式进度，降级到轮询 ──────────────────────────
      const sseSupported = typeof EventSource !== 'undefined'

      if (sseSupported) {
        return new Promise((resolve) => {
          const es = watchPptProgress(request.id, {
            onProgress(data) {
              if (onProgress) {
                onProgress({
                  progress: typeof data.progress === 'number' ? data.progress : Number(data.progress) || 0,
                  stage: data.stage || '生成中',
                  step: data.step || ''
                })
              }
            },
            async onDone(data) {
              if (onProgress) onProgress({ progress: 100, stage: '生成完成', step: 'PPT 已成功生成！' })
              await dispatch('fetchPptHistory')
              // 从历史记录里找出最新这条的完整信息
              try {
                const res = await pptAPI.getRequest(request.id)
                const req = res.data?.request
                if (req) {
                  resolve({ request: normalizeRequest(req), preview: null })
                  return
                }
              } catch (_) {}
              resolve({ request: { ...normalized, status: 'completed' }, preview: null })
            },
            async onFailed(data) {
              await dispatch('fetchPptHistory')
              const err = new Error(data.step || 'PPT 生成失败，请稍后重试')
              err.isGenerationFailed = true
              resolve(Promise.reject(err))
            },
            onTimeout() {
              resolve({ timedOut: true, requestId: request.id })
            },
            onError() {
              // SSE 连接失败，降级到轮询模式
              resolve({ timedOut: true, requestId: request.id })
            }
          })
          // 最长等待 10 分钟，SSE 超时后关闭（后端已控制，此处双保险）
          setTimeout(() => {
            es.close()
            resolve({ timedOut: true, requestId: request.id })
          }, 600000)
        })
      }

      // ── 降级：轮询模式（浏览器不支持 SSE 时）────────────────────────────
      const pollIntervalMs = 2000
      const timeoutMs = 300000
      const start = Date.now()
      while (Date.now() - start < timeoutMs) {
        await new Promise(r => setTimeout(r, pollIntervalMs))
        try {
          const res = await pptAPI.getRequest(request.id)
          const req = res.data?.request
          if (!req) continue
          const status = req.status

          const STAGE_LABELS = {
            init: '初始化', outline: '生成大纲', layout: '分析版式',
            slides: '生成内容', images: '配图生成', rendering: '渲染文件',
            finishing: '收尾处理', done: '生成完成', error: '生成失败',
            brief: 'AI 创意分析', content: '生成内容', render: '渲染文件', finish: '收尾处理'
          }
          if (onProgress && (req.progress !== undefined || req.stage)) {
            const numProgress = req.progress !== undefined ? Number(req.progress) : 50
            const stageLabel = STAGE_LABELS[req.stage] || req.stage || '生成中'
            onProgress({
              progress: isNaN(numProgress) ? 50 : numProgress,
              stage: stageLabel,
              step: req.step || ''
            })
          }

          if (status === 'completed') {
            if (onProgress) onProgress({ progress: 100, stage: '生成完成', step: 'PPT 已成功生成！' })
            await dispatch('fetchPptHistory')
            return { request: normalizeRequest(req), preview: null, warn: req.warn || null }
          }
          if (status === 'failed') {
            await dispatch('fetchPptHistory')
            const reason = req.errorReason || 'PPT 生成失败，请稍后重试'
            const err = new Error(reason)
            err.isGenerationFailed = true
            throw err
          }
        } catch (err) {
          if (err?.isGenerationFailed) throw err
        }
      }
      await dispatch('fetchPptHistory')
      return { timedOut: true, requestId: request.id }
    },
    async fetchModels({ commit, state }) {
      if (state.models.length) {
        return state.models
      }
      commit('setLoading', { key: 'models', value: true })
      try {
        const response = await modelsAPI.list()
        const items = response.data?.items || []
        commit('setModels', items)
        if (!state.selectedModel && items.length) {
          commit('setSelectedModel', items[0].id)
        }
        return items
      } finally {
        commit('setLoading', { key: 'models', value: false })
      }
    },
    updateDefaultModel({ commit }, modelId) {
      commit('setSelectedModel', modelId)
    },
    async deletePptRequest({ commit }, requestId) {
      if (!requestId) {
        return
      }
      await pptAPI.remove(requestId)
      commit('removePptRequest', requestId)
    },
    async fetchTemplates({ state, commit }) {
      if (!state.token && state.templates.length) {
        return state.templates
      }
      commit('setLoading', { key: 'templates', value: true })
      try {
        const response = await templatesAPI.list()
        const items = response.data?.items || []
        commit('setTemplates', items)
        return items
      } finally {
        commit('setLoading', { key: 'templates', value: false })
      }
    },
    async login({ commit, dispatch }, { username, password, rememberMe = true }) {
      try {
        const response = await authAPI.login({ username, password })
        commit('setToken', { token: response.data.token, remember: rememberMe })
        commit('setUser', response.data.user)
        try {
          const tasks = [dispatch('fetchPptHistory'), dispatch('fetchTemplates'), dispatch('fetchModels')]
          if (response.data?.user?.isAdmin) {
            tasks.push(dispatch('fetchAdminHistory'))
          }
          await Promise.all(tasks)
        } catch (fetchError) {
          console.error('登录后加载数据失败:', fetchError)
        }
        return response
      } catch (error) {
        commit('setUser', null)
        commit('setToken', null)
        throw error
      }
    },
    async register({ commit, dispatch }, userData) {
      try {
        const response = await authAPI.register(userData)
        commit('setToken', response.data.token)
        commit('setUser', response.data.user)
        try {
          const tasks = [dispatch('fetchPptHistory'), dispatch('fetchTemplates'), dispatch('fetchModels')]
          if (response.data?.user?.isAdmin) {
            tasks.push(dispatch('fetchAdminHistory'))
          }
          await Promise.all(tasks)
        } catch (fetchError) {
          console.error('注册后加载数据失败:', fetchError)
        }
        return response
      } catch (error) {
        commit('setUser', null)
        commit('setToken', null)
        throw error
      }
    },
    async logout({ commit }) {
      try {
        await authAPI.logout()
      } catch (error) {
        console.error('登出API调用失败:', error)
      } finally {
        commit('logout')
      }
    }
  },
  getters: {
    currentUser: state => state.user,
    isAuthenticated: state => state.isAuthenticated,
    pptHistory: state => state.pptHistory,
    historyLoading: state => state.loading.history,
    adminHistory: state => state.adminHistory,
    adminHistoryLoading: state => state.loading.adminHistory,
    templates: state => state.templates,
    templatesLoading: state => state.loading.templates,
    models: state => state.models,
    modelsLoading: state => state.loading.models,
    selectedModel: state => state.selectedModel,
    aiSearch: state => state.aiSearch
  }
})
