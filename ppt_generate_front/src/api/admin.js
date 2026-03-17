import { apiClient } from './auth'

/** 为 GET 请求附加时间戳，防止浏览器/代理缓存旧数据 */
const nc = () => ({ _t: Date.now() })

export default {
  // ── PPT 记录与统计 ────────────────────────────────────────────────────────
  pptHistory(params = {}) {
    return apiClient.get('/admin/ppt/history', { params: { ...params, ...nc() } })
  },
  metrics(params = {}) {
    return apiClient.get('/admin/ppt/metrics', { params: { ...params, ...nc() } })
  },

  // ── 用户管理 ──────────────────────────────────────────────────────────────
  users(params = {}) {
    return apiClient.get('/admin/users', { params: { ...params, ...nc() } })
  },
  updateUserStatus(data = {}) {
    return apiClient.post('/admin/users/status', data)
  },

  // ── 素材全局管理 ──────────────────────────────────────────────────────────
  /** 获取全量素材列表，支持 { user_id, status, file_type, page, page_size } */
  materials(params = {}) {
    return apiClient.get('/admin/materials', { params: { ...params, ...nc() } })
  },
  /** 获取素材存储统计 */
  materialStats() {
    return apiClient.get('/admin/materials/stats', { params: nc() })
  },
  /** 预览素材提取内容（含审核结论） */
  getMaterialContent(id) {
    return apiClient.get('/admin/materials/content', { params: { id, ...nc() } })
  },
  /** 构造原始文件的可访问 URL（供 iframe src 或新窗口打开使用） */
  getMaterialFileUrl(id) {
    const token = localStorage.getItem('token') || sessionStorage.getItem('token') || ''
    const base = (import.meta.env.VITE_API_URL || '/api').replace(/\/$/, '')
    return `${base}/admin/materials/file?id=${encodeURIComponent(id)}&token=${encodeURIComponent(token)}&_t=${Date.now()}`
  },
  /** 触发 AI 违规审核，返回 { result, reason, reviewedAt } */
  reviewMaterial(id) {
    return apiClient.post('/admin/materials/review', null, { params: { id } })
  },
  /** 删除单个素材，reason 为管理员填写的删除原因 */
  deleteMaterial(id, reason = '') {
    return apiClient.delete('/admin/materials', { params: { id }, data: { reason } })
  },
  /** 批量删除素材，ids: string[]，reason: 删除原因 */
  batchDeleteMaterials(ids = [], reason = '') {
    return apiClient.post('/admin/materials/batch_delete', { ids, reason })
  },

  // ── 公告管理 ──────────────────────────────────────────────────────────────
  /** 管理员获取全量公告列表（含已过期） */
  announcements(params = {}) {
    return apiClient.get('/admin/announcements', { params: { ...params, ...nc() } })
  },
  /** 创建公告 */
  createAnnouncement(data) {
    return apiClient.post('/admin/announcements', data)
  },
  /** 更新公告 */
  updateAnnouncement(id, data) {
    return apiClient.put('/admin/announcements', data, { params: { id } })
  },
  /** 删除公告 */
  deleteAnnouncement(id) {
    return apiClient.delete('/admin/announcements', { params: { id } })
  },

  // ── 偏好洞察 ──────────────────────────────────────────────────────────────
  /** 获取偏好洞察数据（全量历史） */
  insights() {
    return apiClient.get('/admin/insights', { params: nc() })
  }
}
