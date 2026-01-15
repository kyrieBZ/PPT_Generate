import { apiClient } from './auth'

export default {
  list(params = {}) {
    return apiClient.get('/templates', { params })
  }
}
