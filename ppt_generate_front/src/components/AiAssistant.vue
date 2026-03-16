<template>
  <!-- 悬浮触发按钮 -->
  <div
    class="ai-trigger"
    :class="{ 'is-open': panelVisible, 'is-dragging': isDragging }"
    :style="triggerStyle"
    @mousedown="onDragStart"
    @click.stop="onTriggerClick"
    title="AI 助手 · 四夕"
  >
    <div class="trigger-ring" :class="{ 'has-unread': unreadCount > 0 }"></div>
    <div class="trigger-avatar">
      <!-- 四夕头像 SVG（直接内联，无需组件注册） -->
      <svg :width="44" :height="44" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
        <ellipse cx="32" cy="54" rx="14" ry="9" fill="#0EA5E9"/>
        <path d="M24 47 Q32 53 40 47" stroke="#38BDF8" stroke-width="2" fill="none" stroke-linecap="round"/>
        <circle cx="32" cy="27" r="19" fill="#0EA5E9"/>
        <circle cx="32" cy="29" r="15" fill="#FDE8D8"/>
        <ellipse cx="32" cy="13" rx="15" ry="8" fill="#0369A1"/>
        <ellipse cx="19" cy="21" rx="5" ry="8" fill="#0369A1"/>
        <ellipse cx="45" cy="21" rx="5" ry="8" fill="#0369A1"/>
        <circle cx="23" cy="13" r="3" fill="#F97316"/>
        <circle cx="41" cy="13" r="3" fill="#F97316"/>
        <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
        <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
        <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
        <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
        <ellipse cx="22" cy="33" rx="4" ry="2.5" fill="#FCA5A5" opacity="0.65"/>
        <ellipse cx="42" cy="33" rx="4" ry="2.5" fill="#FCA5A5" opacity="0.65"/>
        <path d="M27 37 Q32 41 37 37" stroke="#E11D48" stroke-width="1.5" stroke-linecap="round" fill="none"/>
      </svg>
    </div>
    <span v-if="unreadCount > 0" class="unread-badge">
      {{ unreadCount > 9 ? '9+' : unreadCount }}
    </span>
  </div>

  <!-- 对话面板 -->
  <Transition name="panel-anim">
    <div
      v-if="panelVisible"
      class="ai-panel"
      :style="panelStyle"
      @click.stop
    >
      <!-- 面板头部 -->
      <div class="panel-header">
        <div class="panel-header-orb panel-header-orb--tl"></div>
        <div class="panel-header-orb panel-header-orb--br"></div>
        <div class="panel-header-inner">
          <div class="panel-hd-avatar">
            <svg width="26" height="26" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
              <ellipse cx="32" cy="54" rx="14" ry="9" fill="rgba(255,255,255,0.3)"/>
              <circle cx="32" cy="27" r="19" fill="rgba(255,255,255,0.25)"/>
              <circle cx="32" cy="29" r="15" fill="#FDE8D8"/>
              <ellipse cx="32" cy="13" rx="15" ry="8" fill="rgba(255,255,255,0.2)"/>
              <ellipse cx="19" cy="21" rx="5" ry="8" fill="rgba(255,255,255,0.2)"/>
              <ellipse cx="45" cy="21" rx="5" ry="8" fill="rgba(255,255,255,0.2)"/>
              <circle cx="23" cy="13" r="3" fill="#F97316"/>
              <circle cx="41" cy="13" r="3" fill="#F97316"/>
              <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
              <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
              <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
              <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
              <ellipse cx="22" cy="33" rx="4" ry="2.5" fill="#FCA5A5" opacity="0.65"/>
              <ellipse cx="42" cy="33" rx="4" ry="2.5" fill="#FCA5A5" opacity="0.65"/>
              <path d="M27 37 Q32 41 37 37" stroke="#E11D48" stroke-width="1.5" stroke-linecap="round" fill="none"/>
            </svg>
            <span class="avatar-online-dot"></span>
          </div>
          <div class="panel-hd-info">
            <div class="panel-hd-name">四夕</div>
            <div class="panel-hd-status">
              <span class="status-pulse"></span>
              <span>{{ currentSessionId ? '会话模式' : '四夕 · PPT 助手' }}</span>
            </div>
          </div>
        </div>
        <div class="panel-hd-actions">
          <!-- 会话列表切换按钮（持久化启用时显示） -->
          <button
            v-if="persistenceEnabled"
            class="hd-btn"
            :class="{ 'hd-btn-active': showSessions }"
            title="会话列表"
            @click="toggleSessions"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="14" height="14">
              <line x1="8" y1="6" x2="21" y2="6"/>
              <line x1="8" y1="12" x2="21" y2="12"/>
              <line x1="8" y1="18" x2="21" y2="18"/>
              <line x1="3" y1="6" x2="3.01" y2="6"/>
              <line x1="3" y1="12" x2="3.01" y2="12"/>
              <line x1="3" y1="18" x2="3.01" y2="18"/>
            </svg>
          </button>
          <!-- 新建会话按钮 -->
          <button
            v-if="persistenceEnabled"
            class="hd-btn"
            title="新建会话"
            @click="handleNewSession"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" width="14" height="14">
              <line x1="12" y1="5" x2="12" y2="19"/>
              <line x1="5" y1="12" x2="19" y2="12"/>
            </svg>
          </button>
          <button class="hd-btn" title="清空对话" @click="clearMessages">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="14" height="14">
              <polyline points="3 6 5 6 21 6"/>
              <path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6"/>
              <path d="M10 11v6M14 11v6"/>
              <path d="M9 6V4a1 1 0 011-1h4a1 1 0 011 1v2"/>
            </svg>
          </button>
          <button class="hd-btn hd-btn-close" title="关闭" @click="closePanel">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" width="14" height="14">
              <line x1="18" y1="6" x2="6" y2="18"/>
              <line x1="6" y1="6" x2="18" y2="18"/>
            </svg>
          </button>
        </div>
      </div>

      <!-- 会话列表侧边栏 -->
      <Transition name="sessions-slide">
        <div v-if="showSessions" class="sessions-sidebar">
          <div class="sessions-hd">
            <span class="sessions-hd-title">历史会话</span>
            <span v-if="sessionsLoading" class="sessions-loading">加载中…</span>
          </div>
          <div class="sessions-list">
            <div
              v-for="s in sessions"
              :key="s.session_id"
              class="session-item"
              :class="{ 'is-active': s.session_id === currentSessionId }"
              @click="handleSwitchSession(s.session_id)"
            >
              <div class="session-item-icon">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="13" height="13">
                  <path d="M21 15a2 2 0 01-2 2H7l-4 4V5a2 2 0 012-2h14a2 2 0 012 2z"/>
                </svg>
              </div>
              <div class="session-item-body">
                <div class="session-item-title">{{ s.title || '新会话' }}</div>
                <div class="session-item-time">{{ formatSessionTime(s.updated_at) }}</div>
              </div>
              <button
                class="session-del-btn"
                title="删除"
                @click.stop="handleDeleteSession(s.session_id)"
              >
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="11" height="11">
                  <line x1="18" y1="6" x2="6" y2="18"/>
                  <line x1="6" y1="6" x2="18" y2="18"/>
                </svg>
              </button>
            </div>
            <div v-if="!sessionsLoading && sessions.length === 0" class="sessions-empty">
              暂无会话，点击 + 新建
            </div>
          </div>
        </div>
      </Transition>

      <!-- 消息区 -->
      <div ref="messagesContainer" class="panel-body">
        <!-- 欢迎屏 -->
        <Transition name="welcome-fade">
          <div v-if="messages.length === 0" class="welcome-screen">
            <div class="welcome-avatar-ring">
              <svg width="52" height="52" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
                <ellipse cx="32" cy="54" rx="14" ry="9" fill="#0EA5E9"/>
                <path d="M24 47 Q32 53 40 47" stroke="#38BDF8" stroke-width="2" fill="none" stroke-linecap="round"/>
                <circle cx="32" cy="27" r="19" fill="#0EA5E9"/>
                <circle cx="32" cy="29" r="15" fill="#FDE8D8"/>
                <ellipse cx="32" cy="13" rx="15" ry="8" fill="#0369A1"/>
                <ellipse cx="19" cy="21" rx="5" ry="8" fill="#0369A1"/>
                <ellipse cx="45" cy="21" rx="5" ry="8" fill="#0369A1"/>
                <circle cx="23" cy="13" r="3" fill="#F97316"/>
                <circle cx="41" cy="13" r="3" fill="#F97316"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <ellipse cx="22" cy="33" rx="4" ry="2.5" fill="#FCA5A5" opacity="0.65"/>
                <ellipse cx="42" cy="33" rx="4" ry="2.5" fill="#FCA5A5" opacity="0.65"/>
                <path d="M27 37 Q32 41 37 37" stroke="#E11D48" stroke-width="1.5" stroke-linecap="round" fill="none"/>
              </svg>
            </div>
            <p class="welcome-title">你好，我是 <strong>四夕</strong></p>
            <p class="welcome-sub">四夕为你服务，做你的 PPT 智能小助手</p>
            <div class="welcome-chips">
              <button class="chip" @click="sendExample('帮我生成一个关于人工智能的PPT，共10页')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="13" height="13">
                  <path d="M12 2l3.09 6.26L22 9.27l-5 4.87 1.18 6.88L12 17.77l-6.18 3.25L7 14.14 2 9.27l6.91-1.01L12 2z"/>
                </svg>
                生成 PPT
              </button>
              <button class="chip" @click="sendExample('查看我的PPT历史记录')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="13" height="13">
                  <path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/>
                  <polyline points="14 2 14 8 20 8"/>
                  <line x1="16" y1="13" x2="8" y2="13"/>
                  <line x1="16" y1="17" x2="8" y2="17"/>
                </svg>
                查看历史
              </button>
              <button class="chip" @click="sendExample('有哪些PPT模板可用')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="13" height="13">
                  <rect x="3" y="3" width="7" height="7"/>
                  <rect x="14" y="3" width="7" height="7"/>
                  <rect x="14" y="14" width="7" height="7"/>
                  <rect x="3" y="14" width="7" height="7"/>
                </svg>
                浏览模板
              </button>
            </div>
          </div>
        </Transition>

        <!-- 消息气泡列表 -->
        <TransitionGroup name="msg-fade" tag="div" class="msg-list">
          <div
            v-for="msg in messages"
            :key="msg.id"
            class="msg-row"
            :class="msg.role === 'user' ? 'msg-row-user' : 'msg-row-ai'"
          >
            <div v-if="msg.role === 'assistant'" class="msg-ai-avatar">
              <svg width="20" height="20" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
                <ellipse cx="32" cy="54" rx="14" ry="9" fill="rgba(255,255,255,0.3)"/>
                <circle cx="32" cy="27" r="19" fill="rgba(255,255,255,0.2)"/>
                <circle cx="32" cy="29" r="15" fill="#FDE8D8"/>
                <ellipse cx="32" cy="13" rx="15" ry="8" fill="rgba(255,255,255,0.2)"/>
                <ellipse cx="19" cy="21" rx="5" ry="8" fill="rgba(255,255,255,0.2)"/>
                <ellipse cx="45" cy="21" rx="5" ry="8" fill="rgba(255,255,255,0.2)"/>
                <circle cx="23" cy="13" r="3" fill="#F97316"/>
                <circle cx="41" cy="13" r="3" fill="#F97316"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <ellipse cx="22" cy="33" rx="4" ry="2.5" fill="#FCA5A5" opacity="0.65"/>
                <ellipse cx="42" cy="33" rx="4" ry="2.5" fill="#FCA5A5" opacity="0.65"/>
                <path d="M27 37 Q32 41 37 37" stroke="#E11D48" stroke-width="1.5" stroke-linecap="round" fill="none"/>
              </svg>
            </div>
            <div class="msg-bubble-wrap">
              <div class="msg-bubble">{{ msg.content }}</div>
              <div class="msg-time">{{ msg.time }}</div>
            </div>
          </div>
        </TransitionGroup>

        <!-- 打字动画 -->
        <Transition name="msg-fade">
          <div v-if="isLoading" class="msg-row msg-row-ai typing-row">
            <div class="msg-ai-avatar">
              <svg width="20" height="20" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
                <circle cx="32" cy="29" r="15" fill="#FDE8D8"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#1E293B"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <path d="M27 37 Q32 41 37 37" stroke="#E11D48" stroke-width="1.5" stroke-linecap="round" fill="none"/>
              </svg>
            </div>
            <div class="msg-bubble-wrap">
              <div class="msg-bubble typing-bubble">
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
              </div>
            </div>
          </div>
        </Transition>
      </div>

      <!-- 输入区 -->
      <div class="panel-footer">
        <div class="input-wrapper" :class="{ 'is-focused': inputFocused, 'is-disabled': isLoading }">
          <textarea
            ref="inputRef"
            v-model="inputText"
            class="chat-input"
            placeholder="输入指令，Enter 发送…"
            rows="1"
            :disabled="isLoading"
            @keydown.enter.exact.prevent="sendMessage"
            @input="autoResize"
            @focus="inputFocused = true"
            @blur="inputFocused = false"
          ></textarea>
          <button
            class="send-btn"
            :class="{ 'can-send': inputText.trim() && !isLoading }"
            :disabled="!inputText.trim() || isLoading"
            @click="sendMessage"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="15" height="15">
              <line x1="22" y1="2" x2="11" y2="13"/>
              <polygon points="22 2 15 22 11 13 2 9 22 2"/>
            </svg>
          </button>
        </div>
        <div class="footer-hint">Shift+Enter 换行</div>
      </div>
    </div>
  </Transition>

  <!-- 操作确认弹窗 -->
  <el-dialog
    v-model="confirmVisible"
    :title="confirmDialogTitle"
    width="420px"
    :close-on-click-modal="false"
    class="tutu-confirm-dialog"
    append-to-body
    @close="onConfirmClose"
  >
    <div class="confirm-body">
      <div class="confirm-icon-wrap">
        <svg viewBox="0 0 48 48" fill="none" width="44" height="44">
          <circle cx="24" cy="24" r="24" fill="#EFF6FF"/>
          <path d="M24 14v12M24 30v2" stroke="#0EA5E9" stroke-width="3" stroke-linecap="round"/>
        </svg>
      </div>
      <p class="confirm-desc">{{ pendingAction?.confirmText }}</p>
      <div v-if="pendingAction?.params" class="confirm-meta">
        <template v-if="pendingAction.intent === 'VIEW_PPT'">
          <div class="meta-row"><span class="meta-key">PPT 名称</span><span class="meta-val">{{ pendingAction.params.ppt_title || pendingAction.params.ppt_id }}</span></div>
        </template>
        <template v-else-if="pendingAction.intent === 'DELETE_PPT'">
          <div class="meta-row"><span class="meta-key">操作对象</span><span class="meta-val">{{ pendingAction.params.ppt_title || pendingAction.params.ppt_id }}</span></div>
        </template>
        <template v-else-if="pendingAction.intent === 'GENERATE_PPT'">
          <div class="meta-row"><span class="meta-key">主题</span><span class="meta-val">{{ pendingAction.params.topic }}</span></div>
          <div v-if="pendingAction.params.page_count" class="meta-row"><span class="meta-key">页数</span><span class="meta-val">{{ pendingAction.params.page_count }} 页</span></div>
          <div v-if="pendingAction.params.style" class="meta-row"><span class="meta-key">风格</span><span class="meta-val">{{ pendingAction.params.style }}</span></div>
        </template>
        <template v-else-if="pendingAction.intent === 'DOWNLOAD_PPT'">
          <div class="meta-row"><span class="meta-key">PPT 名称</span><span class="meta-val">{{ pendingAction.params.ppt_title || pendingAction.params.ppt_id }}</span></div>
        </template>
      </div>
    </div>
    <template #footer>
      <div class="confirm-footer">
        <el-button @click="onCancelAction">取消</el-button>
        <el-button type="primary" :loading="actionLoading" @click="onConfirmAction">确认执行</el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup>
import { ref, computed, watch, nextTick } from 'vue'
import { useStore } from 'vuex'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'

const store  = useStore()
const router = useRouter()

// ── 状态 ──────────────────────────────────────────────
const inputText    = ref('')
const inputFocused = ref(false)
const inputRef     = ref(null)
const messagesContainer = ref(null)
const confirmVisible    = ref(false)
const actionLoading     = ref(false)
const showSessions = ref(false)

// 拖拽
const isDragging = ref(false)
const hasDragged = ref(false)
const pos = ref({ left: window.innerWidth - 88, top: window.innerHeight - 150 })

// ── Store ─────────────────────────────────────────────
const panelVisible       = computed(() => store.getters['assistant/isVisible'])
const messages           = computed(() => store.getters['assistant/messages'])
const isLoading          = computed(() => store.getters['assistant/isLoading'])
const pendingAction      = computed(() => store.getters['assistant/pendingAction'])
const unreadCount        = computed(() => store.getters['assistant/unreadCount'])
const sessions           = computed(() => store.getters['assistant/sessions'])
const sessionsLoading    = computed(() => store.getters['assistant/sessionsLoading'])
const currentSessionId   = computed(() => store.getters['assistant/currentSessionId'])
const persistenceEnabled = computed(() => store.getters['assistant/persistenceEnabled'])

// ── 确认弹窗标题 ──────────────────────────────────────
const confirmDialogTitle = computed(() => {
  const map = {
    VIEW_PPT:      '打开 PPT 编辑器',
    DELETE_PPT:    '确认删除 PPT',
    GENERATE_PPT:  '确认生成 PPT',
    NAVIGATE:      '确认跳转',
    DOWNLOAD_PPT:  '确认下载 PPT',
    LIST_TEMPLATES:'查看模板'
  }
  return map[pendingAction.value?.intent] || '操作确认'
})

// ── 面板定位（四方向自适应） ───────────────────────────
const TW = 64
const TH = 64
const PW = 380
const PH = 560
const GAP = 12
const EDGE = 8

const triggerStyle = computed(() => ({
  left: `${pos.value.left}px`,
  top:  `${pos.value.top}px`
}))

const panelStyle = computed(() => {
  const vw = window.innerWidth
  const vh = window.innerHeight
  const { left, top } = pos.value
  const cx = left + TW / 2
  const cy = top  + TH / 2

  const spaceRight  = vw - left - TW - GAP
  const spaceLeft   = left - GAP

  let panelLeft, panelTop

  if (spaceRight >= PW) {
    panelLeft = left + TW + GAP
  } else if (spaceLeft >= PW) {
    panelLeft = left - PW - GAP
  } else {
    panelLeft = Math.max(EDGE, Math.min(vw - PW - EDGE, cx - PW / 2))
  }

  if (spaceRight >= PW || spaceLeft >= PW) {
    panelTop = cy - PH / 2
  } else if (vh - top - TH - GAP >= PH) {
    panelTop = top + TH + GAP
  } else {
    panelTop = top - PH - GAP
  }

  panelTop  = Math.max(EDGE, Math.min(vh - PH - EDGE, panelTop))
  panelLeft = Math.max(EDGE, Math.min(vw - PW - EDGE, panelLeft))

  return { left: `${panelLeft}px`, top: `${panelTop}px` }
})

// ── 监听 ──────────────────────────────────────────────
watch(pendingAction, (val) => { if (val) confirmVisible.value = true })
watch(messages, () => { nextTick(scrollToBottom) }, { deep: true })
watch(isLoading, () => { nextTick(scrollToBottom) })

function scrollToBottom() {
  if (messagesContainer.value)
    messagesContainer.value.scrollTop = messagesContainer.value.scrollHeight
}

// ── 拖拽 ──────────────────────────────────────────────
function onDragStart(e) {
  if (e.button !== 0) return
  hasDragged.value = false
  const startX = e.clientX
  const startY = e.clientY
  const startLeft = pos.value.left
  const startTop  = pos.value.top

  function onMove(e) {
    const dx = e.clientX - startX
    const dy = e.clientY - startY
    if (Math.abs(dx) > 4 || Math.abs(dy) > 4) {
      isDragging.value = true
      hasDragged.value = true
    }
    if (isDragging.value) {
      pos.value = {
        left: Math.max(EDGE, Math.min(window.innerWidth  - TW - EDGE, startLeft + dx)),
        top:  Math.max(EDGE, Math.min(window.innerHeight - TH - EDGE, startTop  + dy))
      }
    }
  }

  function onUp() {
    isDragging.value = false
    document.removeEventListener('mousemove', onMove)
    document.removeEventListener('mouseup',   onUp)
  }

  document.addEventListener('mousemove', onMove)
  document.addEventListener('mouseup',   onUp)
}

function onTriggerClick() {
  if (hasDragged.value) { hasDragged.value = false; return }
  store.dispatch('assistant/toggle')
  if (!panelVisible.value) {
    nextTick(() => inputRef.value?.focus())
    // 打开时拉取会话列表
    store.dispatch('assistant/fetchSessions')
  }
}

function closePanel()    { store.dispatch('assistant/close') }
function clearMessages() { store.commit('assistant/clearMessages') }

// ── 会话管理 ──────────────────────────────────────────
function toggleSessions() {
  showSessions.value = !showSessions.value
  if (showSessions.value) {
    store.dispatch('assistant/fetchSessions')
  }
}

async function handleNewSession() {
  showSessions.value = false
  const sessionId = await store.dispatch('assistant/newSession')
  if (!sessionId) {
    ElMessage.warning('创建会话失败，已切换到无状态模式')
  }
}

async function handleSwitchSession(sessionId) {
  showSessions.value = false
  await store.dispatch('assistant/switchSession', sessionId)
}

async function handleDeleteSession(sessionId) {
  try {
    await store.dispatch('assistant/removeSession', sessionId)
    ElMessage.success('会话已删除')
  } catch {
    ElMessage.error('删除失败，请稍后重试')
  }
}

function formatSessionTime(isoStr) {
  if (!isoStr) return ''
  try {
    const d = new Date(isoStr)
    const now = new Date()
    const diff = now - d
    if (diff < 60000)       return '刚刚'
    if (diff < 3600000)     return `${Math.floor(diff / 60000)} 分钟前`
    if (diff < 86400000)    return `${Math.floor(diff / 3600000)} 小时前`
    return d.toLocaleDateString('zh-CN', { month: 'numeric', day: 'numeric' })
  } catch {
    return ''
  }
}

// ── 发送消息 ──────────────────────────────────────────
async function sendMessage() {
  const text = inputText.value.trim()
  if (!text || isLoading.value) return
  inputText.value = ''
  nextTick(() => { if (inputRef.value) inputRef.value.style.height = 'auto' })
  await store.dispatch('assistant/chat', text)
}

function sendExample(text) { inputText.value = text; sendMessage() }

function autoResize(e) {
  const el = e.target
  el.style.height = 'auto'
  el.style.height = Math.min(el.scrollHeight, 120) + 'px'
}

// ── 确认/取消 ─────────────────────────────────────────
async function onConfirmAction() {
  const action = pendingAction.value
  if (!action) return
  actionLoading.value = true
  try { await executeAction(action) }
  finally {
    actionLoading.value = false
    confirmVisible.value = false
    store.dispatch('assistant/confirmAction')
  }
}

function onCancelAction() {
  confirmVisible.value = false
  store.dispatch('assistant/cancelAction')
  store.commit('assistant/addMessage', { role: 'assistant', content: '好的，已取消该操作。有其他需要帮忙的吗？' })
}

function onConfirmClose() {
  if (pendingAction.value) store.dispatch('assistant/cancelAction')
}

// ── 执行操作 ──────────────────────────────────────────
async function executeAction({ intent, params }) {
  switch (intent) {
    case 'VIEW_PPT': {
      const id = params.ppt_id
      if (!id) {
        ElMessage.warning('未找到对应的 PPT，请提供更准确的名称')
        store.commit('assistant/addMessage', { role: 'assistant', content: '抱歉，我没有找到该 PPT，请提供更准确的名称或前往历史记录查看。' })
        return
      }
      router.push(`/main/edit/${id}`)
      store.dispatch('assistant/close')
      store.commit('assistant/addMessage', { role: 'assistant', content: `已为您打开 PPT「${params.ppt_title || id}」，正在跳转到编辑器…` })
      break
    }
    case 'DELETE_PPT': {
      const id = params.ppt_id
      if (!id) {
        ElMessage.warning('未找到对应的 PPT，请重新描述')
        store.commit('assistant/addMessage', { role: 'assistant', content: '抱歉，我没有找到对应的 PPT 记录，请提供更准确的名称或 ID。' })
        return
      }
      try {
        await store.dispatch('deletePptRequest', id)
        ElMessage.success('PPT 已成功删除')
        store.commit('assistant/addMessage', { role: 'assistant', content: `PPT「${params.ppt_title || id}」已成功删除！` })
      } catch {
        ElMessage.error('删除失败，请稍后重试')
        store.commit('assistant/addMessage', { role: 'assistant', content: '删除操作失败，请稍后重试或手动在历史记录中删除。' })
      }
      break
    }
    case 'GENERATE_PPT': {
      const query = {}
      if (params.topic)      query.topic = params.topic
      if (params.page_count) query.pages = String(params.page_count)
      if (params.style)      query.style = params.style
      router.push({ path: '/main/generate', query })
      store.dispatch('assistant/close')
      store.commit('assistant/addMessage', { role: 'assistant', content: `好的！已为您跳转到生成页面，主题「${params.topic}」已预填，请确认后点击生成。` })
      break
    }
    case 'NAVIGATE': {
      const pageMap = { history: '/main/history', generate: '/main/generate', materials: '/main/materials', templates: '/main/templates', models: '/main/models' }
      router.push(pageMap[params.page] || '/main')
      store.dispatch('assistant/close')
      store.commit('assistant/addMessage', { role: 'assistant', content: '已为您跳转！' })
      break
    }
    case 'DOWNLOAD_PPT': {
      const id = params.ppt_id
      if (!id) { ElMessage.warning('未找到对应的 PPT'); return }
      window.open(`/api/ppt/file?id=${encodeURIComponent(id)}`, '_blank')
      store.commit('assistant/addMessage', { role: 'assistant', content: `已开始下载 PPT「${params.ppt_title || id}」！` })
      break
    }
    case 'LIST_TEMPLATES': {
      router.push('/main/templates')
      store.dispatch('assistant/close')
      store.commit('assistant/addMessage', { role: 'assistant', content: '已为您跳转到模板页面！' })
      break
    }
  }
}
</script>

<style scoped>
/* ── 悬浮触发按钮 ─────────────────────────────────── */
.ai-trigger {
  position: fixed;
  z-index: 8000;
  width: 64px;
  height: 64px;
  cursor: pointer;
  user-select: none;
  display: flex;
  align-items: center;
  justify-content: center;
}

/* 脉冲光环 */
.trigger-ring {
  position: absolute;
  inset: -5px;
  border-radius: 50%;
  border: 2px solid rgba(14, 165, 233, 0.28);
  pointer-events: none;
  animation: ring-float 3.2s ease-in-out infinite;
}

.trigger-ring.has-unread {
  border-color: rgba(14, 165, 233, 0.6);
  animation: ring-pulse 1.8s ease-in-out infinite;
}

@keyframes ring-float {
  0%, 100% { transform: translateY(0) scale(1);    opacity: 0.55; }
  50%       { transform: translateY(-5px) scale(1.03); opacity: 1; }
}

@keyframes ring-pulse {
  0%, 100% { transform: scale(1);    opacity: 0.7; }
  50%       { transform: scale(1.1); opacity: 1; }
}

.ai-trigger.is-open .trigger-ring,
.ai-trigger.is-dragging .trigger-ring {
  animation-play-state: paused;
}

/* 头像容器 */
.trigger-avatar {
  width: 56px;
  height: 56px;
  border-radius: 50%;
  background: linear-gradient(145deg, #0284C7 0%, #0EA5E9 55%, #38BDF8 100%);
  box-shadow:
    0 4px 18px rgba(14, 165, 233, 0.42),
    0 1px 4px rgba(0, 0, 0, 0.10),
    inset 0 1px 0 rgba(255, 255, 255, 0.22);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  transition: box-shadow 0.2s ease, transform 0.18s ease;
  position: relative;
  z-index: 1;
}

.ai-trigger:hover .trigger-avatar {
  box-shadow:
    0 6px 26px rgba(14, 165, 233, 0.58),
    0 2px 8px rgba(0, 0, 0, 0.12),
    inset 0 1px 0 rgba(255, 255, 255, 0.28);
  transform: translateY(-2px) scale(1.05);
}

.ai-trigger.is-open .trigger-avatar {
  box-shadow: 0 2px 10px rgba(14, 165, 233, 0.32);
  transform: scale(0.95);
}

.ai-trigger.is-dragging .trigger-avatar {
  transform: scale(1.08);
  box-shadow: 0 8px 30px rgba(14, 165, 233, 0.55);
  cursor: grabbing;
}

/* 未读徽标 */
.unread-badge {
  position: absolute;
  top: 0;
  right: 0;
  background: linear-gradient(135deg, #F97316, #EF4444);
  color: white;
  font-size: 10px;
  font-weight: 700;
  min-width: 18px;
  height: 18px;
  border-radius: 9px;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0 4px;
  border: 2px solid white;
  box-shadow: 0 2px 6px rgba(239, 68, 68, 0.45);
  z-index: 2;
  animation: badge-pop 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
}

@keyframes badge-pop {
  from { transform: scale(0); }
  to   { transform: scale(1); }
}

/* ── 对话面板 ─────────────────────────────────────── */
.ai-panel {
  position: fixed;
  z-index: 8001;
  width: 380px;
  height: 560px;
  background: #FFFFFF;
  border-radius: 20px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  /* 为会话侧边栏提供 absolute 定位上下文 */
  box-shadow:
    0 0 0 1px rgba(14, 165, 233, 0.10),
    0 4px 8px rgba(0, 0, 0, 0.04),
    0 12px 30px rgba(0, 0, 0, 0.09),
    0 30px 60px rgba(14, 165, 233, 0.07);
}

/* 面板弹出动画 */
.panel-anim-enter-active { transition: all 0.3s cubic-bezier(0.34, 1.56, 0.64, 1); }
.panel-anim-leave-active { transition: all 0.18s ease-in; }
.panel-anim-enter-from   { opacity: 0; transform: scale(0.86) translateY(22px); }
.panel-anim-leave-to     { opacity: 0; transform: scale(0.93) translateY(8px); }

/* ── 面板头部 ─────────────────────────────────────── */
.panel-header {
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 13px 14px 12px;
  background: linear-gradient(135deg, #0369A1 0%, #0EA5E9 60%, #38BDF8 100%);
  position: relative;
  overflow: hidden;
}

/* 装饰光球 */
.panel-header-orb {
  position: absolute;
  border-radius: 50%;
  pointer-events: none;
}
.panel-header-orb--tl {
  top: -28px; right: -18px;
  width: 100px; height: 100px;
  background: rgba(255, 255, 255, 0.07);
}
.panel-header-orb--br {
  bottom: -38px; left: 12px;
  width: 80px; height: 80px;
  background: rgba(255, 255, 255, 0.05);
}

.panel-header-inner {
  display: flex;
  align-items: center;
  gap: 10px;
  position: relative;
  z-index: 1;
}

.panel-hd-avatar {
  width: 38px;
  height: 38px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.18);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  border: 1.5px solid rgba(255, 255, 255, 0.28);
  position: relative;
  overflow: visible;
}

.avatar-online-dot {
  position: absolute;
  bottom: -1px;
  right: -1px;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #4ADE80;
  border: 2px solid rgba(3, 105, 161, 0.8);
  box-shadow: 0 0 0 2px rgba(74, 222, 128, 0.35);
}

.panel-hd-info {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.panel-hd-name {
  font-size: 14px;
  font-weight: 700;
  color: white;
  letter-spacing: 0.2px;
  line-height: 1.2;
}

.panel-hd-status {
  font-size: 11px;
  color: rgba(255, 255, 255, 0.82);
  display: flex;
  align-items: center;
  gap: 5px;
}

.status-pulse {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #4ADE80;
  flex-shrink: 0;
  animation: pulse-status 2.2s ease-in-out infinite;
}

@keyframes pulse-status {
  0%, 100% { opacity: 1;   box-shadow: 0 0 0 0   rgba(74, 222, 128, 0.5); }
  50%       { opacity: 0.7; box-shadow: 0 0 0 4px rgba(74, 222, 128, 0); }
}

.panel-hd-actions {
  display: flex;
  align-items: center;
  gap: 4px;
  position: relative;
  z-index: 1;
}

.hd-btn {
  width: 30px;
  height: 30px;
  border: none;
  background: rgba(255, 255, 255, 0.12);
  border-radius: 8px;
  color: rgba(255, 255, 255, 0.88);
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: background 0.15s, transform 0.1s;
}

.hd-btn:hover {
  background: rgba(255, 255, 255, 0.24);
  color: white;
  transform: scale(1.06);
}

.hd-btn:active { transform: scale(0.92); }

.hd-btn-close:hover { background: rgba(239, 68, 68, 0.65); }

/* ── 头部按钮激活态 ───────────────────────────────── */
.hd-btn-active {
  background: rgba(255, 255, 255, 0.30) !important;
  color: white !important;
}

/* ── 会话列表侧边栏 ───────────────────────────────── */
.sessions-sidebar {
  position: absolute;
  top: 58px; /* 头部高度 */
  left: 0;
  right: 0;
  bottom: 0;
  background: white;
  z-index: 10;
  display: flex;
  flex-direction: column;
  border-top: 1px solid #F1F5F9;
}

.sessions-slide-enter-active { transition: transform 0.22s cubic-bezier(0.34, 1.2, 0.64, 1), opacity 0.18s ease; }
.sessions-slide-leave-active { transition: transform 0.16s ease-in, opacity 0.14s ease-in; }
.sessions-slide-enter-from   { transform: translateX(-100%); opacity: 0; }
.sessions-slide-leave-to     { transform: translateX(-100%); opacity: 0; }

.sessions-hd {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 11px 14px 9px;
  border-bottom: 1px solid #F1F5F9;
  flex-shrink: 0;
}

.sessions-hd-title {
  font-size: 12.5px;
  font-weight: 600;
  color: #475569;
  letter-spacing: 0.3px;
}

.sessions-loading {
  font-size: 11px;
  color: #94A3B8;
}

.sessions-list {
  flex: 1;
  overflow-y: auto;
  padding: 6px 8px;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.sessions-list::-webkit-scrollbar { width: 3px; }
.sessions-list::-webkit-scrollbar-thumb { background: #CBD5E1; border-radius: 2px; }

.session-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 10px;
  border-radius: 10px;
  cursor: pointer;
  transition: background 0.14s;
  position: relative;
}

.session-item:hover { background: #F8FAFC; }

.session-item.is-active {
  background: #EFF6FF;
  border: 1px solid #BAE6FD;
}

.session-item-icon {
  width: 26px;
  height: 26px;
  border-radius: 7px;
  background: #F1F5F9;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  color: #64748B;
}

.session-item.is-active .session-item-icon {
  background: #DBEAFE;
  color: #0284C7;
}

.session-item-body {
  flex: 1;
  min-width: 0;
}

.session-item-title {
  font-size: 12.5px;
  font-weight: 500;
  color: #1E293B;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  line-height: 1.4;
}

.session-item.is-active .session-item-title { color: #0369A1; }

.session-item-time {
  font-size: 10.5px;
  color: #94A3B8;
  margin-top: 1px;
}

.session-del-btn {
  width: 22px;
  height: 22px;
  border: none;
  background: transparent;
  border-radius: 5px;
  color: #CBD5E1;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  opacity: 0;
  transition: all 0.15s;
  flex-shrink: 0;
}

.session-item:hover .session-del-btn { opacity: 1; }
.session-del-btn:hover { background: #FEE2E2; color: #EF4444; }

.sessions-empty {
  text-align: center;
  padding: 32px 16px;
  font-size: 12.5px;
  color: #94A3B8;
  line-height: 1.6;
}

/* ── 消息区 ───────────────────────────────────────── */
.panel-body {
  flex: 1;
  overflow-y: auto;
  padding: 14px 12px;
  background: #F8FAFC;
  display: flex;
  flex-direction: column;
  gap: 0;
}

.panel-body::-webkit-scrollbar { width: 3px; }
.panel-body::-webkit-scrollbar-track { background: transparent; }
.panel-body::-webkit-scrollbar-thumb { background: #CBD5E1; border-radius: 2px; }
.panel-body::-webkit-scrollbar-thumb:hover { background: #94A3B8; }

/* 欢迎屏 */
.welcome-screen {
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  padding: 22px 16px 10px;
  gap: 8px;
}

.welcome-fade-enter-active { transition: all 0.3s ease-out; }
.welcome-fade-leave-active { transition: all 0.2s ease-in; }
.welcome-fade-enter-from   { opacity: 0; transform: translateY(12px); }
.welcome-fade-leave-to     { opacity: 0; }

.welcome-avatar-ring {
  width: 72px;
  height: 72px;
  border-radius: 50%;
  background: linear-gradient(135deg, #E0F2FE, #BAE6FD);
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 4px;
  box-shadow:
    0 4px 16px rgba(14, 165, 233, 0.18),
    0 0 0 6px rgba(14, 165, 233, 0.06);
}

.welcome-title {
  font-size: 15px;
  font-weight: 600;
  color: #0F172A;
  line-height: 1.4;
}

.welcome-title strong { color: #0EA5E9; }

.welcome-sub {
  font-size: 12.5px;
  color: #64748B;
  line-height: 1.55;
}

.welcome-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 7px;
  justify-content: center;
  margin-top: 8px;
}

.chip {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 12px;
  font-weight: 500;
  color: #0369A1;
  background: white;
  border: 1.5px solid #BAE6FD;
  border-radius: 20px;
  padding: 6px 13px;
  cursor: pointer;
  transition: all 0.18s ease;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.05);
}

.chip:hover {
  background: #EFF6FF;
  border-color: #7DD3FC;
  color: #0284C7;
  transform: translateY(-1px);
  box-shadow: 0 3px 10px rgba(14, 165, 233, 0.18);
}

.chip:active { transform: translateY(0) scale(0.97); }

/* 消息列表 */
.msg-list {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.msg-row {
  display: flex;
  align-items: flex-end;
  gap: 7px;
}

.msg-row-user { flex-direction: row-reverse; }

.msg-ai-avatar {
  width: 30px;
  height: 30px;
  border-radius: 50%;
  background: linear-gradient(135deg, #0284C7, #38BDF8);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  box-shadow: 0 2px 8px rgba(14, 165, 233, 0.30);
  overflow: hidden;
}

.msg-bubble-wrap {
  display: flex;
  flex-direction: column;
  gap: 3px;
  max-width: 264px;
}

.msg-row-user .msg-bubble-wrap { align-items: flex-end; }

.msg-bubble {
  padding: 9px 13px;
  border-radius: 16px;
  font-size: 13.5px;
  line-height: 1.58;
  word-break: break-word;
  white-space: pre-wrap;
}

.msg-row-ai .msg-bubble {
  background: white;
  color: #1E293B;
  border-bottom-left-radius: 5px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.06), 0 0 0 1px rgba(0, 0, 0, 0.04);
}

.msg-row-user .msg-bubble {
  background: linear-gradient(135deg, #0284C7, #0EA5E9);
  color: white;
  border-bottom-right-radius: 5px;
  box-shadow: 0 3px 12px rgba(14, 165, 233, 0.35);
}

.msg-time {
  font-size: 10px;
  color: #94A3B8;
  padding: 0 3px;
}

/* 打字动画 */
.typing-row { margin-top: 2px; }

.typing-bubble {
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 12px 16px;
  min-width: 60px;
}

.dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #7DD3FC;
  animation: bounce-dot 1.3s ease-in-out infinite;
}
.dot:nth-child(2) { animation-delay: 0.18s; }
.dot:nth-child(3) { animation-delay: 0.36s; }

@keyframes bounce-dot {
  0%, 60%, 100% { transform: translateY(0);    opacity: 0.4; background: #7DD3FC; }
  30%            { transform: translateY(-7px); opacity: 1;   background: #0EA5E9; }
}

/* 消息淡入 */
.msg-fade-enter-active { transition: all 0.24s ease-out; }
.msg-fade-enter-from   { opacity: 0; transform: translateY(10px); }
.msg-fade-leave-active { transition: all 0.15s ease-in; }
.msg-fade-leave-to     { opacity: 0; }

/* ── 输入区 ───────────────────────────────────────── */
.panel-footer {
  flex-shrink: 0;
  padding: 10px 12px 12px;
  background: white;
  border-top: 1px solid #F1F5F9;
  display: flex;
  flex-direction: column;
  gap: 5px;
}

.input-wrapper {
  display: flex;
  align-items: flex-end;
  gap: 8px;
  background: #F8FAFC;
  border: 1.5px solid #E2E8F0;
  border-radius: 14px;
  padding: 8px 8px 8px 13px;
  transition: border-color 0.18s, box-shadow 0.18s, background 0.18s;
}

.input-wrapper.is-focused {
  border-color: #0EA5E9;
  background: white;
  box-shadow: 0 0 0 3px rgba(14, 165, 233, 0.10);
}

.input-wrapper.is-disabled { opacity: 0.55; }

.chat-input {
  flex: 1;
  border: none;
  background: transparent;
  padding: 0;
  font-size: 13.5px;
  font-family: inherit;
  resize: none;
  outline: none;
  line-height: 1.5;
  max-height: 120px;
  overflow-y: auto;
  color: #1E293B;
}

.chat-input::placeholder { color: #94A3B8; }
.chat-input:disabled { cursor: not-allowed; }

.send-btn {
  width: 34px;
  height: 34px;
  border: none;
  border-radius: 10px;
  background: #E2E8F0;
  color: #94A3B8;
  cursor: not-allowed;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: all 0.18s ease;
}

.send-btn.can-send {
  background: linear-gradient(135deg, #0284C7, #0EA5E9);
  color: white;
  cursor: pointer;
  box-shadow: 0 3px 10px rgba(14, 165, 233, 0.38);
}

.send-btn.can-send:hover {
  transform: scale(1.07);
  box-shadow: 0 4px 14px rgba(14, 165, 233, 0.52);
}

.send-btn.can-send:active { transform: scale(0.93); }

.footer-hint {
  font-size: 10.5px;
  color: #CBD5E1;
  text-align: right;
  padding-right: 2px;
  user-select: none;
}

/* ── 确认弹窗内容 ─────────────────────────────────── */
.confirm-body {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 14px;
  padding: 6px 4px 4px;
  text-align: center;
}

.confirm-icon-wrap { margin-bottom: 2px; }

.confirm-desc {
  font-size: 14.5px;
  color: #374151;
  line-height: 1.65;
  max-width: 300px;
}

.confirm-meta {
  width: 100%;
  background: #F0F9FF;
  border: 1px solid #BAE6FD;
  border-radius: 12px;
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 7px;
  text-align: left;
}

.meta-row {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  font-size: 13px;
}

.meta-key {
  color: #0284C7;
  font-weight: 600;
  flex-shrink: 0;
  min-width: 60px;
}

.meta-val {
  color: #1E293B;
  word-break: break-all;
}

.confirm-footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}

/* 无障碍：减少动画 */
@media (prefers-reduced-motion: reduce) {
  .trigger-ring,
  .status-pulse,
  .dot { animation: none !important; }

  .panel-anim-enter-active,
  .panel-anim-leave-active,
  .msg-fade-enter-active,
  .msg-fade-leave-active,
  .welcome-fade-enter-active,
  .welcome-fade-leave-active {
    transition: opacity 0.01ms !important;
  }
}
</style>

<style>
/* 确认弹窗全局样式（高 z-index，避免被助手遮挡） */
.tutu-confirm-dialog { z-index: 9100 !important; }

.tutu-confirm-dialog .el-dialog {
  border-radius: 20px;
  overflow: hidden;
  box-shadow: 0 8px 40px rgba(0,0,0,0.15), 0 2px 8px rgba(0,0,0,0.07);
}

.tutu-confirm-dialog .el-dialog__header {
  background: linear-gradient(135deg, #0369A1, #0EA5E9);
  padding: 15px 22px;
  margin: 0;
}

.tutu-confirm-dialog .el-dialog__title {
  color: white;
  font-weight: 700;
  font-size: 15px;
}

.tutu-confirm-dialog .el-dialog__headerbtn {
  top: 13px;
  right: 16px;
}

.tutu-confirm-dialog .el-dialog__headerbtn .el-dialog__close {
  color: rgba(255,255,255,0.8);
  font-size: 16px;
}

.tutu-confirm-dialog .el-dialog__headerbtn:hover .el-dialog__close { color: white; }

.tutu-confirm-dialog .el-dialog__body { padding: 22px 24px 8px; }

.tutu-confirm-dialog .el-dialog__footer {
  padding: 12px 24px 20px;
  border-top: 1px solid #F1F5F9;
}
</style>
