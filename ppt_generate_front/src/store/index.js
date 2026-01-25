import { createStore } from 'vuex'
import authAPI, { setAuthToken } from '@/api/auth'
import pptAPI from '@/api/ppt'
import templatesAPI from '@/api/templates'
import modelsAPI from '@/api/models'
import adminAPI from '@/api/admin'

const savedToken = localStorage.getItem('token') || sessionStorage.getItem('token')
const savedModel = localStorage.getItem('defaultModel') || 'qwen-turbo'
if (savedToken) {
  setAuthToken(savedToken)
}

const normalizeRequest = (item = {}) => {
  const id = item.id ?? 0
  const hasFile = Boolean(item.hasFile ?? item.has_file)
  const downloadUrl = item.downloadUrl ?? item.download_url ?? (hasFile && id ? `/api/ppt/file?id=${id}` : '')
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
    downloadUrl
  }
}

export default createStore({
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
    }
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
    async createPptRequest({ commit, state }, payload) {
      const body = { ...payload }
      if (!body.modelId) {
        body.modelId = state.selectedModel || 'qwen-turbo'
      }
      const response = await pptAPI.generate(body)
      const preview = response.data?.preview || null
      if (response.data?.request) {
        const normalized = normalizeRequest(response.data.request)
        commit('prependPptRequest', normalized)
        return { request: normalized, preview }
      }
      return { preview }
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
    selectedModel: state => state.selectedModel
  }
})
