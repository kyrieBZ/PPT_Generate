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
  /** 批量禁用/启用用户，ids: number[], disabled: bool */
  batchUpdateUserStatus(ids = [], disabled = false) {
    return apiClient.post('/admin/users/batch_status', { ids, disabled })
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
  },

  // ── 操作审计日志 ──────────────────────────────────────────────────────────
  /**
   * 分页查询审计日志
   * params: { action?, start?, end?, q?, page?, page_size? }
   */
  auditLogs(params = {}) {
    return apiClient.get('/admin/audit_logs', { params: { ...params, ...nc() } })
  },
  /**
   * 导出审计日志 CSV（返回可直接用于 window.open 的 URL）
   * params: { action?, start?, end?, q? }
   */
  exportAuditLogs(params = {}) {
    const token = localStorage.getItem('token') || sessionStorage.getItem('token') || ''
    const base = (import.meta.env.VITE_API_URL || '/api').replace(/\/$/, '')
    const qs = new URLSearchParams({ ...params, token, _t: Date.now() }).toString()
    return `${base}/admin/export/audit_logs?${qs}`
  },

  // ── 数据导出（模块七）────────────────────────────────────────────────────
  /**
   * 导出 PPT 生成记录 CSV
   * params: { q? }
   */
  exportPptHistory(params = {}) {
    const token = localStorage.getItem('token') || sessionStorage.getItem('token') || ''
    const base = (import.meta.env.VITE_API_URL || '/api').replace(/\/$/, '')
    const qs = new URLSearchParams({ ...params, token, _t: Date.now() }).toString()
    return `${base}/admin/export/ppt_history?${qs}`
  },
  /**
   * 导出用户列表 CSV
   * params: { q? }
   */
  exportUsers(params = {}) {
    const token = localStorage.getItem('token') || sessionStorage.getItem('token') || ''
    const base = (import.meta.env.VITE_API_URL || '/api').replace(/\/$/, '')
    const qs = new URLSearchParams({ ...params, token, _t: Date.now() }).toString()
    return `${base}/admin/export/users?${qs}`
  },

  // ── 系统配置中心 ──────────────────────────────────────────────────────────
  /** 获取所有系统配置项（含元数据） */
  getSettings() {
    return apiClient.get('/admin/settings', { params: nc() })
  },
  /**
   * 批量更新系统配置项
   * data: { key: value, ... }（值统一以字符串/bool/int 形式传入）
   */
  updateSettings(data = {}) {
    return apiClient.put('/admin/settings', data)
  },

  // ── OfficePLUS 模板导入 ───────────────────────────────────────────────────
  /**
   * 搜索 OfficePLUS 模板列表（后端代理抓取）
   * params: { keyword?, tag?, page?, page_size?, cookie? }
   */
  officeplusSearch(params = {}) {
    return apiClient.get('/admin/officeplus/search', { params: { ...params, ...nc() } })
  },
  /**
   * 获取单个模板详情（用于预览确认）
   * params: { url?, id?, cookie? }
   */
  officeplusInfo(params = {}) {
    return apiClient.get('/admin/officeplus/info', { params: { ...params, ...nc() } })
  },
  /**
   * 导入模板（下载 pptx 并写入 catalog）
   * data: { url?, id?, customId?, cookie? }
   */
  officeplusImport(data = {}) {
    return apiClient.post('/admin/officeplus/import', data)
  },
  /**
   * 热重载模板 catalog（导入后调用，无需重启服务）
   */
  officeplusReload() {
    return apiClient.post('/admin/officeplus/reload', {})
  },
  /**
   * 上传本地 pptx 文件到指定 template_id
   * data: { template_id, file_base64 }
   */
  officeplusUpload(data = {}) {
    return apiClient.post('/admin/officeplus/upload', data)
  },
  /**
   * 批量上传本地 pptx，自动写入 catalog
   * data: { files: [ { filename, file_base64 }, ... ] }
   */
  officeplusBatchUpload(data = {}, axiosConfig = {}) {
    return apiClient.post('/admin/officeplus/batch_upload', data, axiosConfig)
  },
  /**
   * 单文件 multipart 上传（用于并发批量）
   * @param {File}     file         — pptx 文件对象
   * @param {string}   displayName  — 中文展示名
   * @param {Function} onProgress   — (percent: number) => void
   */
  officeplusUploadForm(file, displayName, onProgress) {
    const formData = new FormData()
    formData.append('file', file, file.name)
    if (displayName) formData.append('display_name', displayName)
    return apiClient.post('/admin/officeplus/batch_upload_form', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
      onUploadProgress(e) {
        if (onProgress && e.total) onProgress(Math.round(e.loaded / e.total * 95))
      },
    })
  },

  // ── AI 检索索引管理 ───────────────────────────────────────────────────────
  /** 触发全量重建向量索引（异步后台任务，返回 202） */
  reindexPpt() {
    return apiClient.post('/admin/ppt/reindex', {})
  },
  /** 查询索引状态：{ running, indexed_count, vector_available } */
  indexStatus() {
    return apiClient.get('/admin/ppt/index_status', { params: nc() })
  },

  // ── 模板管理 ──────────────────────────────────────────────────────────────
  /**
   * 获取全部模板列表（含上架状态）
   * 返回 { items: [ { id, name, provider, description, previewImage, tags, hasLocalFile, isListed, listing? } ] }
   */
  templateList() {
    return apiClient.get('/admin/templates', { params: nc() })
  },
  /**
   * 上架/更新模板
   * data: { templateId, availableFrom?, availableTo? }
   *   availableFrom: ISO 8601 字符串，默认 NOW()
   *   availableTo:   ISO 8601 字符串，空/不传 = 永久
   */
  activateTemplate(data = {}) {
    return apiClient.post('/admin/templates/activate', data)
  },
  /**
   * 下架模板
   * data: { templateId }
   */
  deactivateTemplate(data = {}) {
    return apiClient.post('/admin/templates/deactivate', data)
  },
  /**
   * 仅删除数据库上架记录（保留文件和 catalog 条目）
   * templateId: string
   */
  removeTemplateRecord(templateId) {
    return apiClient.delete('/admin/templates', { params: { templateId } })
  },
  /**
   * 彻底删除模板：FastDFS 文件 + DB 记录 + catalog 条目 + 本地文件
   * templateId: string
   */
  fullDeleteTemplate(templateId) {
    return apiClient.delete('/admin/templates/full', { params: { templateId } })
  },
  /**
   * 批量同步本地缩略图到 FastDFS
   * force=true 时强制覆盖已有记录
   */
  syncTemplateThumbnails(force = false) {
    return apiClient.post('/admin/templates/sync_thumbnails', {}, {
      params: force ? { force: '1' } : {}
    })
  },
  /**
   * 查询单个模板的 FastDFS 同步状态
   * 返回 { templateId, synced, hasPptx, hasThumb, pptxUrl, thumbUrl }
   */
  templateSyncStatus(templateId) {
    return apiClient.get('/admin/templates/sync_status', { params: { templateId } })
  }
}
