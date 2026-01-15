import { apiClient } from './auth'

export default {
  generate(payload) {
    return apiClient.post('/ppt/generate', payload)
  },
  history() {
    return apiClient.get('/ppt/history')
  },
  remove(id) {
    return apiClient.delete('/ppt/history', {
      params: { id }
    })
  }
}
