import { apiClient } from './auth'

/**
 * 创建 PPT 生成进度的 SSE 连接（P4.1 流式响应）。
 *
 * 注意：浏览器 EventSource API 不支持携带自定义 Header（如 Authorization），
 * 因此通过 URL query 参数传递 token。
 *
 * @param {string|number} requestId  PPT 请求 ID
 * @param {Object} handlers          事件处理回调
 * @param {Function} handlers.onProgress  ({ progress, stage, step, status }) => void
 * @param {Function} handlers.onDone      ({ pptId, title }) => void
 * @param {Function} handlers.onFailed    ({ step }) => void
 * @param {Function} handlers.onTimeout   () => void
 * @param {Function} handlers.onError     (err) => void
 * @returns {EventSource}  可调用 .close() 手动关闭
 */
export function watchPptProgress(requestId, handlers = {}) {
  const token = localStorage.getItem('token') || sessionStorage.getItem('token') || ''
  const url = `/api/ppt/progress/stream?id=${encodeURIComponent(requestId)}&token=${encodeURIComponent(token)}`
  const es = new EventSource(url)

  es.addEventListener('progress', (e) => {
    try {
      const data = JSON.parse(e.data)
      handlers.onProgress?.(data)
    } catch (_) {}
  })

  es.addEventListener('done', (e) => {
    try {
      const data = JSON.parse(e.data)
      handlers.onDone?.(data)
    } catch (_) {}
    es.close()
  })

  es.addEventListener('failed', (e) => {
    try {
      const data = JSON.parse(e.data)
      handlers.onFailed?.(data)
    } catch (_) {}
    es.close()
  })

  es.addEventListener('timeout', () => {
    handlers.onTimeout?.()
    es.close()
  })

  es.addEventListener('error', (e) => {
    handlers.onError?.(e)
    es.close()
  })

  return es
}

export default {
  generate(payload) {
    return apiClient.post('/ppt/generate', payload)
  },

  /**
   * 多模态图片生成 PPT（F04）
   * @param {Object} payload
   * @param {string[]} payload.images  Base64 编码的图片数组（不含 data URI 前缀亦可）
   * @param {string}   payload.hint    用户补充说明（可选）
   * @param {string}   payload.topic   主题覆盖（可选，优先级高于 AI 推断）
   * @param {number}   payload.pages   页数（1-30，默认 10）
   * @param {string}   payload.style   风格（business/academic/creative/minimal）
   * @param {boolean}  payload.include_images  是否配图（默认 true）
   * @param {boolean}  payload.include_charts  是否图表（默认 true）
   * @param {boolean}  payload.include_notes   是否备注（默认 false）
   * @param {string}   payload.template_id     指定模板 ID（可选）
   */
  generateFromImage(payload) {
    return apiClient.post('/ppt/generate-from-image', payload)
  },

  /**
   * F10 风格迁移：上传参考 PPTX，分析并返回 StyleSpec
   * @param {File} file  .pptx 参考文件
   */
  analyzeStyle(file) {
    const form = new FormData()
    form.append('file', file)
    return apiClient.post('/ppt/analyze-style', form, {
      headers: { 'Content-Type': 'multipart/form-data' },
      timeout: 60000,
    })
  },

  // Kept for potential future use (server-side ZIP for non-S3 deployments)
  batchDownloadZip(ids) {
    return apiClient.post('/ppt/batch_download', { ids })
  },

  /** 轮询单条请求状态（用于异步生成），返回 { request } */
  getRequest(id) {
    return apiClient.get('/ppt/request', { params: { id } })
  },
  history(params = {}) {
    return apiClient.get('/ppt/history', { params })
  },
  preview(id) {
    return apiClient.get(`/ppt/preview?id=${encodeURIComponent(id)}`)
  },
  outline(payload) {
    return apiClient.post('/ppt/outline', payload)
  },
  remove(id) {
    return apiClient.delete(`/ppt/history?id=${encodeURIComponent(id)}`)
  },
  /** 批量删除历史记录，ids: number[] */
  batchDelete(ids) {
    return apiClient.post('/ppt/batch_delete', { ids })
  },

  // 在线编辑：获取 / 保存结构化 PPT JSON + 再生成
  getStructure(id) {
    return apiClient.get('/ppt/structure', { params: { id } })
  },
  saveStructure(id, payload) {
    return apiClient.put('/ppt/structure', payload, { params: { id } })
  },
  regenerateFromStructure(id, payload) {
    return apiClient.post('/ppt/structure/regenerate', payload, { params: { id } })
  },

  // AI 语义检索
  aiSearch(query, topK = 10, enableRerank = true) {
    return apiClient.post('/ppt/ai_search', {
      query,
      top_k: topK,
      enable_rerank: enableRerank
    })
  }
}

export const templateApi = {
  /**
   * AI 智能模板推荐（F07）
   * @param {string} topic  用户输入的主题描述
   * @param {number} topK   返回推荐数量（默认 5）
   */
  recommend(topic, topK = 5) {
    return apiClient.post('/templates/recommend', { topic, top_k: topK })
  },

  /**
   * 管理员：全量重建模板向量索引
   */
  reindex() {
    return apiClient.post('/admin/templates/reindex')
  }
}

export const materialApi = {
  /**
   * 手动触发指定素材的 RAG 向量化索引
   * @param {string} materialId  素材 ID
   */
  ragIndex(materialId) {
    return apiClient.post(`/material/rag_index?id=${encodeURIComponent(materialId)}`)
  },

  /**
   * 查询用户知识库索引状态
   */
  ragStatus() {
    return apiClient.get('/material/rag_status')
  }
}
