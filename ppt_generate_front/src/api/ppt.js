import { apiClient } from './auth'

export default {
  generate(payload) {
    return apiClient.post('/ppt/generate', payload)
  },
  history(params = {}) {
    return apiClient.get('/ppt/history', { params })
  },
  preview(id) {
    return apiClient.get(`/ppt/preview?id=${encodeURIComponent(id)}`)
  },
  remove(id) {
    return apiClient.delete(`/ppt/history?id=${encodeURIComponent(id)}`)
  }
}
