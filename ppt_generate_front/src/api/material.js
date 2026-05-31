import { apiClient } from './auth'

const nc = () => ({ _t: Date.now() })

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
    return apiClient.get('/material/status', { params: { id, ...nc() } })
  },

  /** 获取提取结果（含 extractResult 字段） */
  getResult(id) {
    return apiClient.get('/material/result', { params: { id, ...nc() } })
  },

  /** 用户修改并保存提取结果 */
  saveResult(id, extractResult) {
    return apiClient.put('/material/result', { extractResult }, { params: { id } })
  },

  /** 获取用户材料列表 */
  list() {
    return apiClient.get('/material/list', { params: nc() })
  },

  /** 删除材料 */
  remove(id) {
    return apiClient.delete('/material', { params: { id } })
  },

  /** 批量删除材料，ids: string[] */
  batchDelete(ids) {
    return apiClient.post('/material/batch_delete', { ids })
  },

  /** 获取当前用户未读的管理员删除通知 */
  getDeletionNotices() {
    return apiClient.get('/material/notices', { params: nc() })
  },

  /** 标记通知已读，ids 为空数组则标记全部 */
  markNoticesRead(ids = []) {
    return apiClient.post('/material/notices/read', { ids })
  }
}

/**
 * 图片素材库 API
 */
export const imageMaterialApi = {
  /**
   * 上传图片素材（multipart/form-data，字段名 "file"）
   * @param {File} file
   * @param {Function} onProgress — (percent: number) => void
   */
  upload(file, onProgress) {
    const formData = new FormData()
    formData.append('file', file)
    return apiClient.post('/material/image/upload', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress(e) {
        if (onProgress && e.total) {
          onProgress(Math.round((e.loaded * 100) / e.total))
        }
      }
    })
  },

  /**
   * 批量上传图片素材（multipart/form-data，字段名 "files[]"）
   * @param {File[]} files
   * @param {Function} onProgress — (percent: number) => void
   */
  batchUpload(files, onProgress) {
    const formData = new FormData()
    files.forEach((file) => formData.append('files[]', file))
    return apiClient.post('/material/image/batch_upload', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress(e) {
        if (onProgress && e.total) {
          onProgress(Math.round((e.loaded * 100) / e.total))
        }
      }
    })
  },

  /** 获取当前用户图片素材列表 */
  list() {
    return apiClient.get('/material/image/list')
  },

  /** 查询单条图片状态 */
  getStatus(id) {
    return apiClient.get('/material/image/status', { params: { id } })
  },

  /** 获取图片文件 URL（用于 <img> 预览） */
  fileUrl(id) {
    const token = localStorage.getItem('token') || sessionStorage.getItem('token') || ''
    return `/api/material/image/file?id=${encodeURIComponent(id)}&token=${encodeURIComponent(token)}`
  },

  /** 删除图片素材 */
  remove(id) {
    return apiClient.delete('/material/image', { params: { id } })
  },

  /** 批量删除图片素材 */
  batchDelete(ids) {
    return apiClient.post('/material/image/batch_delete', { ids })
  }
}
