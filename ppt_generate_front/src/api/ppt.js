import { apiClient } from './auth'

export default {
  generate(payload) {
    return apiClient.post('/ppt/generate', payload)
  },
  /** 轮询单条请求状态（用于异步生成），返回 { request } */
  getRequest(id) {
    return apiClient.get('/ppt/request', { params: { id } })
  },
  history(params = {}) {
    return apiClient.get('/ppt/history', { params })
  },
  preview(id) {
    return apiClient.get(`/ppt/preview?id=${encodeURIComponent(id)}`)
  },
  outline(payload) {
    return apiClient.post('/ppt/outline', payload)
  },
  remove(id) {
    return apiClient.delete(`/ppt/history?id=${encodeURIComponent(id)}`)
  },
  // 在线编辑：获取 / 保存结构化 PPT JSON + 再生成
  getStructure(id) {
    return apiClient.get('/ppt/structure', { params: { id } })
  },
  saveStructure(id, payload) {
    return apiClient.put('/ppt/structure', payload, { params: { id } })
  },
  regenerateFromStructure(id, payload) {
    return apiClient.post('/ppt/structure/regenerate', payload, { params: { id } })
  }
}
