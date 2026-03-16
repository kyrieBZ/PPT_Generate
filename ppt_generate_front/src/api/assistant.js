import { apiClient } from './auth'

/**
 * 无状态对话（向后兼容，前端自维护历史）
 */
export function sendMessage(message, context = {}) {
  return apiClient.post('/assistant/chat', { message, context })
}

// ── 会话管理 ──────────────────────────────────────────────────────────────────

/**
 * 创建新会话
 * @returns {Promise<{ session_id, title, created_at, updated_at }>}
 */
export function createSession() {
  return apiClient.post('/assistant/sessions')
}

/**
 * 获取当前用户的会话列表
 * @param {number} limit 最大返回数量，默认 20
 * @returns {Promise<{ sessions: Array }>}
 */
export function listSessions(limit = 20) {
  return apiClient.get('/assistant/sessions', { params: { limit } })
}

/**
 * 获取某会话的历史消息
 * @param {string} sessionId
 * @param {number} limit 最大条数，默认 50
 * @returns {Promise<{ session_id, messages: Array }>}
 */
export function getMessages(sessionId, limit = 50) {
  return apiClient.get(`/assistant/sessions/${sessionId}/messages`, {
    params: { limit }
  })
}

/**
 * 在会话内发送消息（持久化版本）
 * @param {string} sessionId
 * @param {string} message
 * @param {object} context
 * @returns {Promise<{ reply, action }>}
 */
export function chatInSession(sessionId, message, context = {}) {
  return apiClient.post(`/assistant/sessions/${sessionId}/chat`, { message, context })
}

/**
 * 删除会话及其所有消息
 * @param {string} sessionId
 */
export function deleteSession(sessionId) {
  return apiClient.delete(`/assistant/sessions/${sessionId}`)
}
