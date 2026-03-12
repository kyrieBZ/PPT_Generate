import { sendMessage } from '@/api/assistant'

const MAX_MESSAGES = 50

export default {
  namespaced: true,
  state: () => ({
    visible: false,
    messages: [],
    loading: false,
    pendingAction: null,
    unreadCount: 0
  }),
  mutations: {
    setVisible(state, value) {
      state.visible = value
      if (value) {
        state.unreadCount = 0
      }
    },
    addMessage(state, message) {
      state.messages.push({
        id: Date.now() + Math.random(),
        role: message.role,
        content: message.content,
        time: new Date().toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })
      })
      if (state.messages.length > MAX_MESSAGES) {
        state.messages.splice(0, state.messages.length - MAX_MESSAGES)
      }
      if (!state.visible && message.role === 'assistant') {
        state.unreadCount += 1
      }
    },
    setLoading(state, value) {
      state.loading = value
    },
    setPendingAction(state, action) {
      state.pendingAction = action
    },
    clearMessages(state) {
      state.messages = []
      state.unreadCount = 0
    }
  },
  actions: {
    open({ commit }) {
      commit('setVisible', true)
    },
    close({ commit }) {
      commit('setVisible', false)
    },
    toggle({ state, commit }) {
      commit('setVisible', !state.visible)
    },
    async chat({ commit, state, rootState }, message) {
      if (!message || !message.trim()) return

      commit('addMessage', { role: 'user', content: message.trim() })
      commit('setLoading', true)

      const recentPpts = (rootState.pptHistory || []).slice(0, 10).map(p => ({
        id: p.id,
        title: p.title || p.topic,
        created_at: p.createdAt
      }))

      const context = {
        recent_ppts: recentPpts
      }

      // 携带最近 10 条对话历史作为上下文
      const historyMessages = state.messages.slice(-10).map(m => ({
        role: m.role,
        content: m.content
      }))
      if (historyMessages.length > 0) {
        context.history = historyMessages
      }

      try {
        const response = await sendMessage(message.trim(), context)
        const { reply, action } = response.data

        commit('addMessage', { role: 'assistant', content: reply })

        if (action && action.intent && action.intent !== 'UNKNOWN') {
          commit('setPendingAction', {
            intent: action.intent,
            params: action.params || {},
            confirmText: action.confirm_text || `确认执行此操作吗？`,
            reply
          })
        }
      } catch (err) {
        const errMsg = err?.response?.data?.message || '抱歉，我暂时无法响应，请稍后再试。'
        commit('addMessage', { role: 'assistant', content: errMsg })
      } finally {
        commit('setLoading', false)
      }
    },
    confirmAction({ commit }) {
      commit('setPendingAction', null)
    },
    cancelAction({ commit }) {
      commit('setPendingAction', null)
    }
  },
  getters: {
    isVisible: state => state.visible,
    messages: state => state.messages,
    isLoading: state => state.loading,
    pendingAction: state => state.pendingAction,
    unreadCount: state => state.unreadCount
  }
}
