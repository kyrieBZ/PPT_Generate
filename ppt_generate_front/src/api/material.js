import { apiClient } from './auth'

export default {
  /**
   * 上传文档文件（PDF / DOCX / TXT）
   * @param {File} file
   * @param {Function} onProgress  — (percent: number) => void
   */
  upload(file, onProgress) {
    const formData = new FormData()
    formData.append('file', file)
    return apiClient.post('/material/upload', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress(e) {
        if (onProgress && e.total) {
          onProgress(Math.round((e.loaded * 100) / e.total))
        }
      }
    })
  },

  /** 轮询提取状态 */
  getStatus(id) {
    return apiClient.get('/material/status', { params: { id } })
  },

  /** 获取提取结果（含 extractResult 字段） */
  getResult(id) {
    return apiClient.get('/material/result', { params: { id } })
  },

  /** 用户修改并保存提取结果 */
  saveResult(id, extractResult) {
    return apiClient.put('/material/result', { extractResult }, { params: { id } })
  },

  /** 获取用户材料列表 */
  list() {
    return apiClient.get('/material/list')
  },

  /** 删除材料 */
  remove(id) {
    return apiClient.delete('/material', { params: { id } })
  }
}
