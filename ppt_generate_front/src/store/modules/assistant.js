import {
  sendMessage,
  createSession,
  listSessions,
  getMessages,
  chatInSession,
  deleteSession
} from '@/api/assistant'

const MAX_MESSAGES = 100

export default {
  namespaced: true,
  state: () => ({
    visible: false,
    // 当前会话 ID（持久化模式），null 表示无状态模式
    currentSessionId: null,
    // 当前会话的消息列表（UI 展示用）
    messages: [],
    loading: false,
    pendingAction: null,
    unreadCount: 0,
    // 会话列表（侧边栏）
    sessions: [],
    sessionsLoading: false,
    // 是否已启用持久化（由首次操作结果判断）
    persistenceEnabled: true,
  }),

  mutations: {
    setVisible(state, value) {
      state.visible = value
      if (value) state.unreadCount = 0
    },
    setCurrentSession(state, sessionId) {
      state.currentSessionId = sessionId
    },
    setMessages(state, messages) {
      state.messages = messages
    },
    addMessage(state, message) {
      state.messages.push({
        id: Date.now() + Math.random(),
        role: message.role,
        content: message.content,
        timestamp: message.timestamp || null,
        time: message.time || new Date().toLocaleTimeString('zh-CN', {
          hour: '2-digit', minute: '2-digit'
        })
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
    },
    setSessions(state, sessions) {
      state.sessions = sessions
    },
    setSessionsLoading(state, value) {
      state.sessionsLoading = value
    },
    addSession(state, session) {
      state.sessions.unshift(session)
    },
    removeSession(state, sessionId) {
      state.sessions = state.sessions.filter(s => s.session_id !== sessionId)
    },
    updateSessionTitle(state, { sessionId, title }) {
      const s = state.sessions.find(s => s.session_id === sessionId)
      if (s) s.title = title
    },
    setPersistenceEnabled(state, value) {
      state.persistenceEnabled = value
    }
  },

  actions: {
    open({ commit }) { commit('setVisible', true) },
    close({ commit }) { commit('setVisible', false) },
    toggle({ state, commit }) { commit('setVisible', !state.visible) },

    // ── 会话管理 ────────────────────────────────────────────────────────────

    /** 拉取会话列表 */
    async fetchSessions({ commit }) {
      commit('setSessionsLoading', true)
      try {
        const res = await listSessions(30)
        commit('setSessions', res.data.sessions || [])
        commit('setPersistenceEnabled', true)
      } catch (err) {
        // MongoDB 未启用时后端返回 503，静默降级
        if (err?.response?.status === 503) {
          commit('setPersistenceEnabled', false)
        }
      } finally {
        commit('setSessionsLoading', false)
      }
    },

    /** 创建新会话并切换到该会话 */
    async newSession({ commit, dispatch }) {
      try {
        const res = await createSession()
        const session = res.data
        commit('addSession', session)
        commit('setCurrentSession', session.session_id)
        commit('setMessages', [])
        commit('setPersistenceEnabled', true)
        return session.session_id
      } catch (err) {
        if (err?.response?.status === 503) {
          commit('setPersistenceEnabled', false)
        }
        return null
      }
    },

    /** 切换到指定会话，加载其历史消息 */
    async switchSession({ commit }, sessionId) {
      commit('setCurrentSession', sessionId)
      commit('setLoading', true)
      try {
        const res = await getMessages(sessionId, 100)
        const msgs = (res.data.messages || []).map(m => ({
          id: Date.now() + Math.random(),
          role: m.role,
          content: m.content,
          timestamp: m.timestamp,
          time: m.timestamp
            ? new Date(m.timestamp).toLocaleTimeString('zh-CN', {
                hour: '2-digit', minute: '2-digit'
              })
            : ''
        }))
        commit('setMessages', msgs)
      } catch {
        commit('setMessages', [])
      } finally {
        commit('setLoading', false)
      }
    },

    /** 删除会话 */
    async removeSession({ commit, state }, sessionId) {
      await deleteSession(sessionId)
      commit('removeSession', sessionId)
      if (state.currentSessionId === sessionId) {
        commit('setCurrentSession', null)
        commit('setMessages', [])
      }
    },

    // ── 发送消息（自动选择持久化 or 无状态） ───────────────────────────────

    async chat({ commit, state, dispatch, rootState }, message) {
      if (!message || !message.trim()) return

      commit('addMessage', { role: 'user', content: message.trim() })
      commit('setLoading', true)

      const recentPpts = (rootState.pptHistory || []).slice(0, 10).map(p => ({
        id: p.id,
        title: p.title || p.topic,
        created_at: p.createdAt
      }))
      const context = { recent_ppts: recentPpts }

      try {
        let reply, action

        if (state.persistenceEnabled && state.currentSessionId) {
          // ── 持久化模式：使用会话 API
          const res = await chatInSession(state.currentSessionId, message.trim(), context)
          reply  = res.data.reply
          action = res.data.action
        } else {
          // ── 无状态模式（降级或 MongoDB 未启用）
          const historyMessages = state.messages.slice(-10).map(m => ({
            role: m.role, content: m.content
          }))
          if (historyMessages.length > 0) context.history = historyMessages

          const res = await sendMessage(message.trim(), context)
          reply  = res.data.reply
          action = res.data.action
        }

        commit('addMessage', { role: 'assistant', content: reply })

        if (action && action.intent && action.intent !== 'UNKNOWN') {
          commit('setPendingAction', {
            intent:      action.intent,
            params:      action.params || {},
            confirmText: action.confirm_text || '确认执行此操作吗？',
            reply
          })
        }
      } catch (err) {
        // 持久化会话 403（session 不存在）时自动新建会话重试一次
        if (err?.response?.status === 403 && state.currentSessionId) {
          commit('setCurrentSession', null)
          commit('setLoading', false)
          return dispatch('chat', message)
        }
        const errMsg = err?.response?.data?.message || '抱歉，我暂时无法响应，请稍后再试。'
        commit('addMessage', { role: 'assistant', content: errMsg })
      } finally {
        commit('setLoading', false)
      }
    },

    confirmAction({ commit }) { commit('setPendingAction', null) },
    cancelAction({ commit })  { commit('setPendingAction', null) }
  },

  getters: {
    isVisible:           state => state.visible,
    messages:            state => state.messages,
    isLoading:           state => state.loading,
    pendingAction:       state => state.pendingAction,
    unreadCount:         state => state.unreadCount,
    sessions:            state => state.sessions,
    sessionsLoading:     state => state.sessionsLoading,
    currentSessionId:    state => state.currentSessionId,
    persistenceEnabled:  state => state.persistenceEnabled
  }
}
