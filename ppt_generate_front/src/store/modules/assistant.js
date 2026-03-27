import {
  sendMessage,
  createSession,
  listSessions,
  getMessages,
  chatInSession,
  deleteSession
} from '@/api/assistant'
import pptApi from '@/api/ppt'

const MAX_MESSAGES = 100

// ── 操作历史：工具名 → 人类可读标签 ─────────────────────────────────────────
function buildOperationLabel(tool, params) {
  switch (tool) {
    case 'navigate_to_page': {
      const pageNames = {
        history: '历史记录', generate: '生成 PPT', materials: '素材管理',
        templates: '模板浏览', profile: '个人中心', dashboard: '仪表盘', settings: '系统设置',
      }
      return `跳转到「${pageNames[params.page] || params.page}」`
    }
    case 'trigger_generate_ppt':
      return `生成 PPT：${params.topic || params.title || '（未命名）'}`
    case 'open_ppt_editor':
      return `打开 PPT 编辑器（ID: ${params.ppt_id}）`
    case 'download_ppt':
      return `下载 PPT（ID: ${params.ppt_id}）`
    case 'batch_delete_ppt': {
      const n = Array.isArray(params.ppt_list) ? params.ppt_list.length : '?'
      return `批量删除 ${n} 个 PPT`
    }
    case 'batch_download_ppt': {
      const n = Array.isArray(params.ppt_list) ? params.ppt_list.length : '?'
      return `批量下载 ${n} 个 PPT`
    }
    case 'show_material_upload':
      return '打开素材上传面板'
    case 'preview_material':
      return `预览素材（ID: ${params.material_id}）`
    case 'fill_login_form':
      return `协助登录${params.username_hint ? '：' + params.username_hint : ''}`
    case 'show_announcement':
      return '查看系统公告'
    case 'toggle_maintenance_mode':
      return params.action === 'enable' ? '开启系统维护模式' : '关闭系统维护模式'
    case 'list_announcements':
      return '查看公告列表'
    case 'create_announcement':
      return `发布公告：${params.title || '（未命名）'}`
    default:
      return tool
  }
}

// ── 客户端工具执行器 ─────────────────────────────────────────────────────────
// 参数：tool（工具名），params（工具参数），store（Vuex store），router（Vue Router 实例）
// 返回 Promise，resolve 表示执行成功
async function executeClientTool(tool, params, store, router) {
  switch (tool) {
    case 'navigate_to_page': {
      const pageMap = {
        history:   '/main/history',
        generate:  '/main/generate',
        materials: '/main/materials',
        templates: '/main/templates',
        profile:   '/main/profile',
        dashboard: '/main/dashboard',
        settings:  '/main/settings',
      }
      const target = pageMap[params.page] || '/main/dashboard'
      if (router) {
        await router.push(target)
      }
      break
    }

    case 'trigger_generate_ppt': {
      // 通过 URL query params 传参，Main.vue 检测后预填并自动触发生成流程
      const query = {}
      if (params.topic)         query.topic         = params.topic
      if (params.title)         query.title         = params.title
      if (params.page_count)    query.pages         = String(params.page_count)
      if (params.style)         query.style         = params.style
      if (params.template_id)   query.template_id   = params.template_id
      if (params.generate_mode) query.generate_mode = params.generate_mode
      // auto_generate 默认 true，只有明确传 false 才不自动触发
      if (params.auto_generate !== false) query.auto_generate = '1'
      if (router) {
        await router.push({ path: '/main/generate', query })
      }
      break
    }

    case 'open_ppt_editor': {
      if (params.ppt_id && router) {
        await router.push(`/main/edit/${params.ppt_id}`)
      }
      break
    }

    case 'download_ppt': {
      if (params.ppt_id) {
        const url = `/api/ppt/file?id=${params.ppt_id}`
        window.open(url, '_blank')
      }
      break
    }

    case 'batch_delete_ppt': {
      // 批量删除：用户已通过确认弹窗确认，调用后端 API 执行删除
      const pptList = Array.isArray(params.ppt_list) ? params.ppt_list : []
      if (pptList.length === 0) break

      const ids = pptList.map(p => Number(p.id)).filter(id => id > 0)
      if (ids.length === 0) break

      try {
        const res = await pptApi.batchDelete(ids)
        const results = res.data?.results || []
        const successCount = results.filter(r => r.status === 'ok').length
        const failedItems = results
          .filter(r => r.status !== 'ok')
          .map(r => {
            const matched = pptList.find(p => Number(p.id) === r.id)
            return { id: String(r.id), title: matched?.title || String(r.id) }
          })
        const deletedItems = results
          .filter(r => r.status === 'ok')
          .map(r => {
            const matched = pptList.find(p => Number(p.id) === r.id)
            return { id: String(r.id), title: matched?.title || String(r.id) }
          })
        store.commit('appendToolCardToLastMessage', {
          card_type: 'batch_delete_result',
          deleted_count: successCount,
          failed_count: failedItems.length,
          deleted_list: deletedItems,
          failed_list: failedItems,
          success: failedItems.length === 0,
        })
        if (successCount > 0) {
          store.commit('addMessage', {
            role: 'assistant',
            content: `已成功删除 ${successCount} 个 PPT${failedItems.length > 0 ? `，${failedItems.length} 个删除失败` : ''}。`,
          })
        }
      } catch (e) {
        store.commit('addMessage', {
          role: 'assistant',
          content: '批量删除时发生错误：' + (e?.response?.data?.message || e.message || '未知错误'),
        })
      }
      break
    }

    case 'batch_download_ppt': {
      // 批量下载：渲染卡片，调用后端打包成 ZIP 后触发下载
      const pptList = Array.isArray(params.ppt_list) ? params.ppt_list : []
      if (pptList.length === 0) break

      // 先渲染卡片（状态：打包中）
      store.commit('appendToolCardToLastMessage', {
        card_type: 'batch_download',
        data: pptList,
        total: pptList.length,
        status: 'packing',
      })

      const downloadable = pptList.filter(p => p.has_file !== false)
      if (downloadable.length === 0) break

      try {
        const { apiClient } = await import('@/api/auth')
        const ids = downloadable.map(p => p.id)

        // 步骤1：POST 打包 ZIP，后端返回轻量 JSON { token, filename, size }
        const prepResp = await apiClient.post('/ppt/batch_download', { ids })
        const { token: zipToken, filename: zipFilename } = prepResp.data

        // 步骤2：用浏览器原生 GET 下载（不经过 XHR），彻底绕过 Vite proxy 大文件传输限制
        const jwtToken = localStorage.getItem('token') || sessionStorage.getItem('token') || ''
        store.commit('updateLastBatchDownloadCard', { status: 'downloading' })
        const downloadUrl = `/api/ppt/batch_zip?token=${encodeURIComponent(zipToken)}&auth=${encodeURIComponent(jwtToken)}`
        const a = document.createElement('a')
        a.href = downloadUrl
        a.download = zipFilename || 'ppt_batch.zip'
        document.body.appendChild(a)
        a.click()
        document.body.removeChild(a)
        store.commit('updateLastBatchDownloadCard', { status: 'done' })
      } catch (e) {
        store.commit('updateLastBatchDownloadCard', { status: 'error' })
      }
      break
    }

    case 'show_material_upload': {
      if (router) {
        await router.push('/main/materials')
      }
      // 通知素材页打开上传面板
      store.commit('setOpenMaterialUpload', true, { root: true })
      break
    }

    case 'preview_material': {
      // 跳转到管理员素材管理页，并通过 query 参数触发预览抽屉
      if (params.material_id && router) {
        await router.push({
          path: '/admin',
          query: { nav: 'materials', preview: params.material_id }
        })
      }
      break
    }

    case 'fill_login_form': {
      if (router) {
        await router.push('/login')
      }
      // 如果有用户名提示，通过 store 传递
      if (params.username_hint) {
        store.commit('setLoginHint', params.username_hint, { root: true })
      }
      break
    }

    case 'show_announcement': {
      // 公告由后端查询后已在 reply 中说明，无需前端额外操作
      break
    }

    case 'toggle_maintenance_mode': {
      // 维护模式开关：用户已通过 CONFIRM 弹窗确认，调用管理员 settings API
      const action = params.action  // "enable" | "disable"
      const reason = params.reason || ''
      const enable = action === 'enable'
      try {
        const { apiClient } = await import('@/api/auth')
        await apiClient.put('/admin/settings', {
          maintenance_mode: enable ? 'true' : 'false'
        })
        const msg = enable
          ? `✅ 系统维护模式已开启${reason ? '，维护原因：' + reason : ''}。所有普通用户现在无法访问系统。`
          : '✅ 系统维护模式已关闭，系统已恢复正常访问。'
        store.commit('addMessage', { role: 'assistant', content: msg })
        store.commit('appendToolCardToLastMessage', {
          card_type: 'maintenance_result',
          enabled: enable,
          reason,
          success: true,
        })
      } catch (e) {
        store.commit('addMessage', {
          role: 'assistant',
          content: '维护模式操作失败：' + (e?.response?.data?.message || e.message || '未知错误'),
        })
      }
      break
    }

    default:
      console.warn('[AssistantStore] 未知客户端工具:', tool)
  }

  // 记录操作历史（成功）
  if (store.commit) {
    store.commit('recordOperation', {
      tool,
      params,
      status: 'success',
      label: buildOperationLabel(tool, params),
    })
  }
}

export default {
  namespaced: true,
  state: () => ({
    visible: false,
    currentSessionId: null,
    messages: [],
    loading: false,
    // 待确认的客户端工具队列（P0 新增：支持多工具按序确认）
    pendingClientTools: [],
    // 当前正在等待确认的工具（从 pendingClientTools 头部取出）
    pendingAction: null,
    unreadCount: 0,
    sessions: [],
    sessionsLoading: false,
    persistenceEnabled: true,
    // 工具结果卡片（附加到最新 AI 消息下方展示）
    latestToolCards: [],
    // P4.6 操作历史：本次会话中已执行的操作时间线
    operationHistory: [],
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
        }),
        // 附加工具结果卡片（仅 assistant 消息）
        toolCards: message.toolCards || [],
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
    // 设置待确认的操作队列
    setPendingClientTools(state, tools) {
      state.pendingClientTools = tools || []
    },
    // 从队列取出第一个（调用后 pendingAction 指向它）
    setPendingAction(state, action) {
      state.pendingAction = action
    },
    // 移除队列头部
    shiftPendingClientTools(state) {
      if (state.pendingClientTools.length > 0) {
        state.pendingClientTools.shift()
      }
    },
    clearMessages(state) {
      state.messages = []
      state.unreadCount = 0
      state.pendingClientTools = []
      state.pendingAction = null
      state.operationHistory = []
    },
    // P4.6 操作历史：清空
    clearOperationHistory(state) {
      state.operationHistory = []
    },
    // P4.6 操作历史记录
    recordOperation(state, { tool, params, status, label }) {
      state.operationHistory.push({
        id: Date.now() + Math.random(),
        tool,
        params: params || {},
        status,   // 'success' | 'cancelled' | 'error'
        label,    // 人类可读的操作摘要
        time: new Date().toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
        timestamp: Date.now(),
      })
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
    },
    setLatestToolCards(state, cards) {
      state.latestToolCards = cards || []
    },
    appendToolCardToLastMessage(state, card) {
      for (let i = state.messages.length - 1; i >= 0; i--) {
        if (state.messages[i].role === 'assistant') {
          state.messages[i].toolCards = [...(state.messages[i].toolCards || []), card]
          break
        }
      }
    },
    // 更新最后一条 assistant 消息中最后一张 batch_download 卡片的状态
    updateLastBatchDownloadCard(state, patch) {
      for (let i = state.messages.length - 1; i >= 0; i--) {
        if (state.messages[i].role === 'assistant') {
          const cards = state.messages[i].toolCards
          if (!Array.isArray(cards)) break
          for (let j = cards.length - 1; j >= 0; j--) {
            if (cards[j].card_type === 'batch_download') {
              state.messages[i].toolCards = cards.map((c, idx) =>
                idx === j ? { ...c, ...patch } : c
              )
              return
            }
          }
          break
        }
      }
    },
  },

  actions: {
    open({ commit }) { commit('setVisible', true) },
    close({ commit }) { commit('setVisible', false) },
    toggle({ state, commit }) { commit('setVisible', !state.visible) },

    // ── 会话管理 ────────────────────────────────────────────────────────────

    async fetchSessions({ commit }) {
      commit('setSessionsLoading', true)
      try {
        const res = await listSessions(30)
        commit('setSessions', res.data.sessions || [])
        commit('setPersistenceEnabled', true)
      } catch (err) {
        if (err?.response?.status === 503) {
          commit('setPersistenceEnabled', false)
        }
      } finally {
        commit('setSessionsLoading', false)
      }
    },

    async newSession({ commit }) {
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
            : '',
          toolCards: Array.isArray(m.tool_cards) ? m.tool_cards : [],
        }))
        commit('setMessages', msgs)
      } catch {
        commit('setMessages', [])
      } finally {
        commit('setLoading', false)
      }
    },

    async removeSession({ commit, state }, sessionId) {
      await deleteSession(sessionId)
      commit('removeSession', sessionId)
      if (state.currentSessionId === sessionId) {
        commit('setCurrentSession', null)
        commit('setMessages', [])
      }
    },

    // ── 发送消息（Tool Call 架构）───────────────────────────────────────────

    async chat({ commit, state, dispatch, rootState }, { message, router }) {
      if (!message || !message.trim()) return

      commit('addMessage', { role: 'user', content: message.trim() })
      commit('setLoading', true)
      commit('setLatestToolCards', [])

      // 构建上下文（扩展版）
      const recentPpts = (rootState.pptHistory || []).slice(0, 10).map(p => ({
        id: p.id,
        title: p.title || p.topic,
        created_at: p.createdAt
      }))
      const context = {
        recent_ppts: recentPpts,
        is_admin: rootState.user?.isAdmin || false,
        current_page: router?.currentRoute?.value?.path || '',
      }

      try {
        let resData

        if (state.persistenceEnabled && state.currentSessionId) {
          const res = await chatInSession(state.currentSessionId, message.trim(), context)
          resData = res.data
        } else {
          // 无状态降级模式
          const historyMessages = state.messages.slice(-10).map(m => ({
            role: m.role, content: m.content
          }))
          if (historyMessages.length > 0) context.history = historyMessages
          const res = await sendMessage(message.trim(), context)
          resData = res.data
        }

        const reply = resData.reply || '操作已完成。'
        const toolCards = resData.tool_results_summary || []
        const pendingClientTools = resData.pending_client_tools || []

        // 保存工具结果卡片
        commit('setLatestToolCards', toolCards)

        // 添加 AI 回复消息（附带工具卡片）
        commit('addMessage', {
          role: 'assistant',
          content: reply,
          toolCards: toolCards,
        })

        // 处理待执行的客户端工具
        if (pendingClientTools.length > 0) {
          await dispatch('processClientTools', { tools: pendingClientTools, router })
        }

      } catch (err) {
        if (err?.response?.status === 403 && state.currentSessionId) {
          commit('setCurrentSession', null)
          commit('setLoading', false)
          return dispatch('chat', { message, router })
        }
        const errMsg = err?.response?.data?.message || '抱歉，我暂时无法响应，请稍后再试。'
        commit('addMessage', { role: 'assistant', content: errMsg })
      } finally {
        commit('setLoading', false)
      }
    },

    // ── 客户端工具处理（按序执行，危险工具等待用户确认）───────────────────

    async processClientTools({ commit, dispatch }, { tools, router }) {
      for (const toolItem of tools) {
        const { tool, params, confirm_required, require_confirm_code, confirm_text } = toolItem

        if (require_confirm_code) {
          // 最高级危险工具：需用户手动输入 "CONFIRM"，弹出特殊输入确认弹窗
          await new Promise((resolve) => {
            commit('setPendingAction', {
              tool,
              params: params || {},
              confirmText: confirm_text || '此操作不可逆，请输入 CONFIRM 以确认执行。',
              requireConfirmCode: true,
              resolve,
            })
          })
        } else if (confirm_required) {
          // 普通危险工具：弹出标准确认弹窗
          await new Promise((resolve) => {
            commit('setPendingAction', {
              tool,
              params: params || {},
              confirmText: confirm_text || '确认执行此操作吗？',
              resolve,  // AiAssistant.vue 调用 confirmAction/cancelAction 时 resolve
            })
          })
        } else {
          // 无需确认：直接执行
          try {
            const store = { commit, dispatch }
            await executeClientTool(tool, params || {}, store, router)
          } catch (e) {
            console.warn('[AssistantStore] 客户端工具执行失败:', tool, e)
            commit('recordOperation', {
              tool,
              params: params || {},
              status: 'error',
              label: buildOperationLabel(tool, params || {}),
            })
          }
        }
      }
    },

    // ── 用户确认操作（AiAssistant.vue 调用）─────────────────────────────────

    async confirmAction({ state, commit, dispatch }, router) {
      const action = state.pendingAction
      if (!action) return
      commit('setPendingAction', null)

      try {
        const store = { commit, dispatch }
        await executeClientTool(action.tool, action.params || {}, store, router)
      } catch (e) {
        console.warn('[AssistantStore] 确认执行工具失败:', e)
      }

      if (typeof action.resolve === 'function') action.resolve()
    },

    // ── 高危工具：用户输入 CONFIRM 后的确认（携带确认码）─────────────────────
    async confirmActionWithCode({ state, commit, dispatch }, { router, confirmCode }) {
      const action = state.pendingAction
      if (!action) return
      if (confirmCode !== 'CONFIRM') {
        // 确认码错误，不执行，弹窗已由前端提示
        return
      }
      commit('setPendingAction', null)

      try {
        const store = { commit, dispatch }
        await executeClientTool(action.tool, action.params || {}, store, router)
      } catch (e) {
        console.warn('[AssistantStore] 高危工具执行失败:', e)
      }

      if (typeof action.resolve === 'function') action.resolve()
    },

    cancelAction({ state, commit }) {
      const action = state.pendingAction
      commit('setPendingAction', null)
      if (action) {
        commit('recordOperation', {
          tool: action.tool,
          params: action.params || {},
          status: 'cancelled',
          label: buildOperationLabel(action.tool, action.params || {}),
        })
        if (typeof action.resolve === 'function') action.resolve()
      }
    },

    confirmActionLegacy({ commit }) { commit('setPendingAction', null) },
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
    persistenceEnabled:  state => state.persistenceEnabled,
    latestToolCards:     state => state.latestToolCards,
    operationHistory:    state => state.operationHistory,
  }
}
