import { apiClient } from './auth'

/** 获取当前有效公告（用户端使用） */
export function getActiveAnnouncements() {
  return apiClient.get('/announcements', { params: { _t: Date.now() } })
}
