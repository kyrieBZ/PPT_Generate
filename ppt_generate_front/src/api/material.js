import { apiClient } from './auth'

export default {
  /**
   * 单文件上传（PDF / DOCX / TXT）
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

  /**
   * 批量上传（一次请求携带多个文件，后端返回 207 Multi-Status）
   * @param {File[]} files           — 文件列表，最多 10 个
   * @param {Function} onProgress   — (percent: number) => void  整体上传进度
   */
  batchUpload(files, onProgress) {
    const formData = new FormData()
    files.forEach((f) => formData.append('files[]', f))
    return apiClient.post('/material/batch_upload', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress(e) {
        if (onProgress && e.total) {
          onProgress(Math.round((e.loaded * 100) / e.total))
        }
      }
    })
  },

  /**
   * 批量轮询提取状态（一次查多个 id）
   * @param {string[]} ids
   */
  batchStatus(ids) {
    return apiClient.get('/material/batch_status', { params: { ids: ids.join(',') } })
  },

  /** 轮询单条提取状态 */
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
