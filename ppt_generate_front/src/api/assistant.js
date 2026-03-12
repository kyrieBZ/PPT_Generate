import { apiClient } from './auth'

/**
 * 发送消息给 AI 助手
 * @param {string} message - 用户输入的自然语言指令
 * @param {object} context - 当前页面上下文（current_page, recent_ppts 等）
 * @returns {Promise} 响应包含 { reply, action }
 */
export function sendMessage(message, context = {}) {
  return apiClient.post('/assistant/chat', { message, context })
}
