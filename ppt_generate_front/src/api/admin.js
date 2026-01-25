import { apiClient } from './auth'

export default {
  pptHistory(params = {}) {
    return apiClient.get('/admin/ppt/history', { params })
  },
  metrics(params = {}) {
    return apiClient.get('/admin/ppt/metrics', { params })
  },
  users(params = {}) {
    return apiClient.get('/admin/users', { params })
  },
  updateUserStatus(data = {}) {
    return apiClient.post('/admin/users/status', data)
  }
}
