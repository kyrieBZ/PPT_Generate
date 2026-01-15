import { apiClient } from './auth'

export default {
  list() {
    return apiClient.get('/models')
  }
}
