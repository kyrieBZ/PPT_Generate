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
        <ellipse cx="32" cy="54" rx="14" ry="9" fill="#171717"/>
        <path d="M24 47 Q32 53 40 47" stroke="#404040" stroke-width="2" fill="none" stroke-linecap="round"/>
        <circle cx="32" cy="27" r="19" fill="#171717"/>
        <circle cx="32" cy="29" r="15" fill="#F5F5F5"/>
        <ellipse cx="32" cy="13" rx="15" ry="8" fill="#000000"/>
        <ellipse cx="19" cy="21" rx="5" ry="8" fill="#000000"/>
        <ellipse cx="45" cy="21" rx="5" ry="8" fill="#000000"/>
        <circle cx="23" cy="13" r="3" fill="#D4AF37"/>
        <circle cx="41" cy="13" r="3" fill="#D4AF37"/>
        <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#171717"/>
        <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#171717"/>
        <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
        <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
        <ellipse cx="22" cy="33" rx="4" ry="2.5" fill="#E5E5E5" opacity="0.65"/>
        <ellipse cx="42" cy="33" rx="4" ry="2.5" fill="#E5E5E5" opacity="0.65"/>
        <path d="M27 37 Q32 41 37 37" stroke="#404040" stroke-width="1.5" stroke-linecap="round" fill="none"/>
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
              <ellipse cx="32" cy="54" rx="14" ry="9" fill="rgba(0,0,0,0.1)"/>
              <circle cx="32" cy="27" r="19" fill="rgba(0,0,0,0.08)"/>
              <circle cx="32" cy="29" r="15" fill="#FFFFFF"/>
              <ellipse cx="32" cy="13" rx="15" ry="8" fill="rgba(0,0,0,0.05)"/>
              <ellipse cx="19" cy="21" rx="5" ry="8" fill="rgba(0,0,0,0.05)"/>
              <ellipse cx="45" cy="21" rx="5" ry="8" fill="rgba(0,0,0,0.05)"/>
              <circle cx="23" cy="13" r="3" fill="#D4AF37"/>
              <circle cx="41" cy="13" r="3" fill="#D4AF37"/>
              <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#171717"/>
              <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#171717"/>
              <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
              <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
              <ellipse cx="22" cy="33" rx="4" ry="2.5" fill="#E5E5E5" opacity="0.65"/>
              <ellipse cx="42" cy="33" rx="4" ry="2.5" fill="#E5E5E5" opacity="0.65"/>
              <path d="M27 37 Q32 41 37 37" stroke="#404040" stroke-width="1.5" stroke-linecap="round" fill="none"/>
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
          <!-- 操作历史时间线按钮（有操作记录时显示小圆点） -->
          <button
            class="hd-btn hd-btn-history"
            :class="{ 'hd-btn-active': showHistory }"
            title="操作历史"
            @click="toggleHistory"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="18" height="18">
              <circle cx="12" cy="12" r="10"/>
              <polyline points="12 6 12 12 16 14"/>
            </svg>
            <span v-if="operationHistory.length > 0" class="hd-btn-history-dot"></span>
          </button>
          <!-- 会话列表切换按钮（持久化启用时显示） -->
          <button
            v-if="persistenceEnabled"
            class="hd-btn"
            :class="{ 'hd-btn-active': showSessions }"
            title="会话列表"
            @click="toggleSessions"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="18" height="18">
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
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" width="18" height="18">
              <line x1="12" y1="5" x2="12" y2="19"/>
              <line x1="5" y1="12" x2="19" y2="12"/>
            </svg>
          </button>
          <!-- 删除当前会话按钮（仅在有活跃会话时显示） -->
          <button
            v-if="persistenceEnabled && currentSessionId"
            class="hd-btn hd-btn-danger"
            title="删除当前会话"
            @click="handleDeleteCurrentSession"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="18" height="18">
              <polyline points="3 6 5 6 21 6"/>
              <path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6"/>
              <path d="M10 11v6M14 11v6"/>
              <path d="M9 6V4a1 1 0 011-1h4a1 1 0 011 1v2"/>
            </svg>
          </button>
          <!-- 清空当前消息（不删除会话） -->
          <button class="hd-btn" title="清空当前显示" @click="clearMessages">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="18" height="18">
              <path d="M4 12h16M4 6h16M4 18h7"/>
              <polyline points="15 15 18 18 21 15"/>
              <line x1="18" y1="18" x2="18" y2="11"/>
            </svg>
          </button>
          <button class="hd-btn hd-btn-close" title="关闭" @click="closePanel">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" width="18" height="18">
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
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="16" height="16">
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
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="14" height="14">
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

      <!-- 操作历史侧边栏（P4.6） -->
      <Transition name="sessions-slide">
        <div v-if="showHistory" class="sessions-sidebar op-history-sidebar">
          <div class="sessions-hd">
            <span class="sessions-hd-title">操作历史</span>
            <button
              class="op-history-clear-btn"
              title="清空记录"
              :disabled="operationHistory.length === 0"
              @click="clearOperationHistory"
            >
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="13" height="13">
                <polyline points="3 6 5 6 21 6"/>
                <path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6"/>
                <path d="M10 11v6M14 11v6"/>
              </svg>
              清空
            </button>
          </div>

          <!-- 时间线列表 -->
          <div class="sessions-list op-history-list">
            <div v-if="operationHistory.length === 0" class="sessions-empty">
              本次会话暂无已执行的操作
            </div>

            <TransitionGroup name="op-item-fade" tag="div" class="op-timeline">
              <div
                v-for="(op, idx) in [...operationHistory].reverse()"
                :key="op.id"
                class="op-item"
                :class="`op-item--${op.status}`"
              >
                <!-- 时间线竖线 + 圆点 -->
                <div class="op-dot-col">
                  <div class="op-dot">
                    <!-- success -->
                    <svg v-if="op.status === 'success'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" width="10" height="10">
                      <polyline points="20 6 9 17 4 12"/>
                    </svg>
                    <!-- cancelled -->
                    <svg v-else-if="op.status === 'cancelled'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" width="10" height="10">
                      <line x1="18" y1="6" x2="6" y2="18"/>
                      <line x1="6" y1="6" x2="18" y2="18"/>
                    </svg>
                    <!-- error -->
                    <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" width="10" height="10">
                      <circle cx="12" cy="12" r="10"/>
                      <line x1="12" y1="8" x2="12" y2="12"/>
                      <line x1="12" y1="16" x2="12.01" y2="16"/>
                    </svg>
                  </div>
                  <div v-if="idx < operationHistory.length - 1" class="op-line"></div>
                </div>

                <!-- 操作内容 -->
                <div class="op-content">
                  <div class="op-label">{{ op.label }}</div>
                  <div class="op-meta">
                    <span class="op-status-tag" :class="`op-status-tag--${op.status}`">
                      {{ op.status === 'success' ? '已执行' : op.status === 'cancelled' ? '已取消' : '失败' }}
                    </span>
                    <span class="op-time">{{ op.time }}</span>
                  </div>
                </div>
              </div>
            </TransitionGroup>
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
                <ellipse cx="32" cy="54" rx="14" ry="9" fill="#171717"/>
                <path d="M24 47 Q32 53 40 47" stroke="#404040" stroke-width="2" fill="none" stroke-linecap="round"/>
                <circle cx="32" cy="27" r="19" fill="#171717"/>
                <circle cx="32" cy="29" r="15" fill="#F5F5F5"/>
                <ellipse cx="32" cy="13" rx="15" ry="8" fill="#000000"/>
                <ellipse cx="19" cy="21" rx="5" ry="8" fill="#000000"/>
                <ellipse cx="45" cy="21" rx="5" ry="8" fill="#000000"/>
                <circle cx="23" cy="13" r="3" fill="#D4AF37"/>
                <circle cx="41" cy="13" r="3" fill="#D4AF37"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <ellipse cx="22" cy="33" rx="4" ry="2.5" fill="#E5E5E5" opacity="0.65"/>
                <ellipse cx="42" cy="33" rx="4" ry="2.5" fill="#E5E5E5" opacity="0.65"/>
                <path d="M27 37 Q32 41 37 37" stroke="#404040" stroke-width="1.5" stroke-linecap="round" fill="none"/>
              </svg>
            </div>
            <p class="welcome-title">你好，我是 <strong>四夕</strong></p>
            <p class="welcome-sub">四夕为你服务，做你的 PPT 智能小助手</p>
            <div class="welcome-chips">
              <button class="chip" @click="sendExample('帮我生成一个关于人工智能的PPT，共10页')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="16" height="16">
                  <path d="M12 2l3.09 6.26L22 9.27l-5 4.87 1.18 6.88L12 17.77l-6.18 3.25L7 14.14 2 9.27l6.91-1.01L12 2z"/>
                </svg>
                生成 PPT
              </button>
              <button class="chip" @click="sendExample('查看我的PPT历史记录')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="16" height="16">
                  <path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/>
                  <polyline points="14 2 14 8 20 8"/>
                  <line x1="16" y1="13" x2="8" y2="13"/>
                  <line x1="16" y1="17" x2="8" y2="17"/>
                </svg>
                查看历史
              </button>
              <button class="chip" @click="sendExample('有哪些PPT模板可用')">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="16" height="16">
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
                <circle cx="32" cy="29" r="15" fill="#171717"/>
                <circle cx="32" cy="29" r="14" fill="#F5F5F5"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <path d="M27 37 Q32 41 37 37" stroke="#404040" stroke-width="1.5" stroke-linecap="round" fill="none"/>
              </svg>
            </div>
            <div class="msg-bubble-wrap">
              <!-- AI 消息渲染 Markdown；用户消息保持纯文本 -->
              <div
                v-if="msg.role === 'assistant'"
                class="msg-bubble msg-bubble-md"
                v-html="renderMarkdown(msg.content)"
              ></div>
              <div v-else class="msg-bubble">{{ msg.content }}</div>
              <!-- 工具结果卡片（仅 assistant 消息且有工具结果时展示） -->
              <div v-if="msg.role === 'assistant' && msg.toolCards && msg.toolCards.length" class="tool-cards">
                <!-- PPT 列表卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'ppt_list')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'ppt_list')"
                    :key="'ppt-' + msg.id"
                    class="tool-card ppt-result-card"
                  >
                    <!-- 卡片头 -->
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="13" height="13"><path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>
                      PPT 检索结果
                      <span class="tool-card-count">{{ Array.isArray(card.data) ? card.data.length : 0 }} 条</span>
                      <span v-if="card.search_mode === 'vector'" class="tool-card-badge badge-vector">AI 语义</span>
                    </div>

                    <!-- 空结果 -->
                    <div v-if="!Array.isArray(card.data) || card.data.length === 0" class="tool-card-empty">
                      没有找到相关 PPT
                    </div>

                    <!-- PPT 条目（完整卡片） -->
                    <div
                      v-for="ppt in (Array.isArray(card.data) ? card.data : [])"
                      :key="ppt.id"
                      class="ppt-result-item"
                    >
                      <!-- 左侧：PPT 缩略占位图 -->
                      <div class="ppt-thumb" :class="pptThumbClass(ppt)">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.4" width="22" height="22">
                          <rect x="2" y="3" width="20" height="14" rx="2"/>
                          <path d="M8 21h8"/><path d="M12 17v4"/>
                        </svg>
                        <span class="ppt-thumb-pages" v-if="ppt.pages">{{ ppt.pages }}P</span>
                      </div>

                      <!-- 右侧：信息区 -->
                      <div class="ppt-info">
                        <div class="ppt-info-title" :title="ppt.title">
                          {{ ppt.title || ppt.topic || '未命名' }}
                        </div>
                        <div class="ppt-info-topic" v-if="ppt.topic && ppt.topic !== ppt.title" :title="ppt.topic">
                          {{ ppt.topic }}
                        </div>
                        <div class="ppt-info-meta">
                          <span v-if="ppt.template_name" class="ppt-meta-tag">{{ ppt.template_name }}</span>
                          <span class="ppt-meta-dot" v-if="ppt.template_name">·</span>
                          <span>{{ formatCardTime(ppt.created_at) }}</span>
                          <span v-if="ppt.score" class="ppt-meta-score">{{ Math.round(ppt.score * 100) }}% 匹配</span>
                        </div>
                        <!-- AI 匹配理由 -->
                        <div v-if="ppt.reason" class="ppt-info-reason">
                          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
                          {{ ppt.reason }}
                        </div>
                        <!-- 操作按钮 -->
                        <div class="ppt-info-actions">
                          <button class="card-btn" @click="router.push(`/main/edit/${ppt.id}`)">
                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
                            编辑
                          </button>
                          <button
                            v-if="ppt.has_file"
                            class="card-btn card-btn-dl"
                            @click="openDownload(ppt.id)"
                          >
                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                            下载
                          </button>
                          <span v-else class="ppt-no-file">生成中…</span>
                        </div>
                      </div>
                    </div>
                  </div>
                </template>
                <!-- 素材列表卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'material_list' && c.is_admin_view)">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'material_list' && c.is_admin_view)"
                    :key="'mat-' + msg.id"
                    class="tool-card"
                  >
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="13" height="13"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
                      全量素材（管理员视角）
                      <span class="tool-card-count">{{ Array.isArray(card.data) ? card.data.length : 0 }} / {{ card.total || (Array.isArray(card.data) ? card.data.length : 0) }} 个</span>
                    </div>
                    <div v-if="!Array.isArray(card.data) || card.data.length === 0" class="tool-card-empty">
                      暂无素材
                    </div>
                    <div
                      v-for="mat in (Array.isArray(card.data) ? card.data : [])"
                      :key="mat.id"
                      class="tool-card-item"
                    >
                      <div class="tool-card-item-row">
                        <div class="tool-card-item-title">{{ mat.filename }}</div>
                        <!-- 审核状态徽章（管理员视角） -->
                        <span
                          v-if="card.is_admin_view && mat.review_status"
                          class="tool-card-badge"
                          :class="{
                            'badge-active': mat.review_status === 'pass',
                            'badge-inactive': mat.review_status === 'violation',
                            'badge-vector': mat.review_status === 'unreviewed'
                          }"
                        >
                          {{ mat.review_status === 'pass' ? '✓ 合规' : mat.review_status === 'violation' ? '✗ 违规' : '未审核' }}
                        </span>
                      </div>
                      <div class="tool-card-item-meta">
                        {{ mat.file_type }} · {{ statusLabel(mat.status) }} · {{ formatFileSize(mat.file_size) }}
                        <span v-if="card.is_admin_view && mat.user_id"> · 用户ID: {{ mat.user_id }}</span>
                      </div>
                      <!-- 审核理由展示 -->
                      <div v-if="card.is_admin_view && mat.review_reason" class="mat-review-reason">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
                        {{ mat.review_reason }}
                      </div>
                      <div class="tool-card-item-actions">
                        <!-- 管理员：审核 + 强制删除 -->
                        <template v-if="card.is_admin_view">
                          <button
                            class="card-btn"
                            @click="previewMaterial(mat)"
                          >
                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg>
                            预览
                          </button>
                          <button
                            class="card-btn"
                            @click="askReviewMaterial(mat)"
                          >
                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><path d="M9 11l3 3L22 4"/><path d="M21 12v7a2 2 0 01-2 2H5a2 2 0 01-2-2V5a2 2 0 012-2h11"/></svg>
                            AI 审核
                          </button>
                          <button
                            class="card-btn card-btn-danger"
                            @click="askAdminDeleteMaterial(mat)"
                          >
                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6"/><path d="M10 11v6"/><path d="M14 11v6"/></svg>
                            强制删除
                          </button>
                        </template>
                        <!-- 普通用户：删除自己的素材 -->
                        <button
                          v-else
                          class="card-btn card-btn-danger"
                          @click="askDeleteMaterial(mat)"
                        >删除</button>
                      </div>
                    </div>
                  </div>
                </template>
                <!-- 我的素材卡片（普通用户视角） -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'my_materials')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'my_materials')"
                    :key="'mymat-' + msg.id"
                    class="tool-card"
                  >
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="13" height="13"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
                      我的素材
                      <span class="tool-card-count">{{ card.total || (Array.isArray(card.data) ? card.data.length : 0) }} 个</span>
                    </div>
                    <div v-if="!Array.isArray(card.data) || card.data.length === 0" class="tool-card-empty">
                      暂无素材，可以说「上传素材」来添加
                    </div>
                    <div
                      v-for="mat in (Array.isArray(card.data) ? card.data : [])"
                      :key="mat.id"
                      class="tool-card-item"
                    >
                      <div class="tool-card-item-row">
                        <div class="tool-card-item-title">{{ mat.filename }}</div>
                        <span
                          class="tool-card-badge"
                          :class="mat.review_status === 'pass' ? 'badge-active' : mat.review_status === 'violation' ? 'badge-danger' : 'badge-pending'"
                        >
                          {{ mat.review_status === 'pass' ? '✓ 合规' : mat.review_status === 'violation' ? '✗ 违规' : '未审核' }}
                        </span>
                      </div>
                      <div class="tool-card-item-meta">
                        {{ mat.file_type }} · {{ statusLabel(mat.status) }} · {{ formatFileSize(mat.file_size) }}
                      </div>
                      <div class="tool-card-item-actions">
                        <button class="card-btn card-btn-danger" @click="askDeleteMaterial(mat)">删除</button>
                      </div>
                    </div>
                  </div>
                </template>
                <!-- 模板列表卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'template_list')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'template_list')"
                    :key="'tmpl-' + msg.id"
                    class="tool-card"
                  >
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="16" height="16"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/></svg>
                      {{ card.admin_view ? '全部模板（管理员视角）' : '可用模板' }}
                      <span class="tool-card-count">{{ Array.isArray(card.data) ? card.data.length : 0 }} 个</span>
                    </div>
                    <div v-if="card.op_message" class="tool-card-op-msg">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="13" height="13"><path d="M5 13l4 4L19 7"/></svg>
                      {{ card.op_message }}
                    </div>
                    <div
                      v-for="tmpl in (Array.isArray(card.data) ? card.data : [])"
                      :key="tmpl.id"
                      class="tool-card-item"
                    >
                      <div class="tool-card-item-row">
                        <div class="tool-card-item-title">{{ tmpl.name }}</div>
                        <span
                          v-if="card.admin_view"
                          class="tool-card-badge"
                          :class="tmpl.is_active ? 'badge-active' : 'badge-inactive'"
                        >{{ tmpl.is_active ? '已上架' : '未上架' }}</span>
                      </div>
                      <div class="tool-card-item-meta">
                        {{ tmpl.provider || '' }}
                        <span v-if="tmpl.tags && tmpl.tags.length">
                          · {{ tmpl.tags.slice(0, 2).join(' / ') }}
                        </span>
                      </div>
                      <!-- 模板描述（用户和管理员都展示） -->
                      <div v-if="tmpl.description" class="tool-card-item-desc">
                        {{ tmpl.description }}
                      </div>
                      <div class="tool-card-item-actions">
                        <!-- 用户视角：使用此模板 -->
                        <template v-if="!card.admin_view">
                          <button
                            class="card-btn"
                            @click="router.push({ path: '/main/generate', query: { template_id: tmpl.id } })"
                          >使用此模板</button>
                        </template>
                        <!-- 管理员视角：上架/下架 -->
                        <template v-else>
                          <button
                            v-if="!tmpl.is_active"
                            class="card-btn"
                            @click="askToggleTemplate(tmpl, 'activate')"
                          >
                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><path d="M5 12l5 5L20 7"/></svg>
                            上架
                          </button>
                          <button
                            v-if="tmpl.is_active"
                            class="card-btn card-btn-warn"
                            @click="askToggleTemplate(tmpl, 'deactivate')"
                          >
                            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                            下架
                          </button>
                        </template>
                      </div>
                    </div>
                  </div>
                </template>
                <!-- 批量下载卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'batch_download')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'batch_download')"
                    :key="'bdl-' + msg.id"
                    class="tool-card"
                  >
                    <!-- 标题栏 + 状态 -->
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="13" height="13"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                      批量下载
                      <span class="tool-card-count">{{ Array.isArray(card.data) ? card.data.length : 0 }} 个</span>
                      <span v-if="card.status === 'packing'" class="bdl-status bdl-status--packing">
                        <svg class="spin-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="11" height="11"><path d="M21 12a9 9 0 11-6.22-8.57"/></svg>
                        打包中…
                      </span>
                      <span v-else-if="card.status === 'downloading'" class="bdl-status bdl-status--packing">
                        <svg class="spin-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="11" height="11"><path d="M21 12a9 9 0 11-6.22-8.57"/></svg>
                        下载中…
                      </span>
                      <span v-else-if="card.status === 'done'" class="bdl-status bdl-status--done">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" width="11" height="11"><polyline points="20 6 9 17 4 12"/></svg>
                        已下载
                      </span>
                      <span v-else-if="card.status === 'error'" class="bdl-status bdl-status--error">下载失败</span>
                    </div>
                    <div v-if="!Array.isArray(card.data) || card.data.length === 0" class="tool-card-empty">
                      没有可下载的 PPT
                    </div>
                    <!-- PPT 列表 -->
                    <div
                      v-for="ppt in (Array.isArray(card.data) ? card.data : [])"
                      :key="ppt.id"
                      class="tool-card-item batch-dl-item"
                    >
                      <div class="tool-card-item-row">
                        <div class="tool-card-item-title">{{ ppt.title || ppt.topic || '未命名' }}</div>
                        <span v-if="ppt.has_file === false" class="ppt-no-file">生成中…</span>
                      </div>
                      <div class="tool-card-item-meta">
                        <span v-if="ppt.pages">{{ ppt.pages }} 页</span>
                        <span v-if="ppt.pages && (ppt.template_name || ppt.created_at)"> · </span>
                        <span v-if="ppt.template_name" class="ppt-meta-tag">{{ ppt.template_name }}</span>
                        <span v-if="ppt.template_name && ppt.created_at"> · </span>
                        <span v-if="ppt.created_at">{{ formatCardTime(ppt.created_at) }}</span>
                        <span v-if="ppt.topic && ppt.topic !== ppt.title" class="ppt-meta-topic"> · {{ ppt.topic }}</span>
                      </div>
                      <div class="tool-card-item-actions" v-if="ppt.has_file !== false">
                        <button class="card-btn card-btn-dl" @click="openDownload(ppt.id)">
                          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                          单独下载
                        </button>
                        <button class="card-btn" @click="router.push(`/main/edit/${ppt.id}`)">
                          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><path d="M11 4H4a2 2 0 00-2 2v14a2 2 0 002 2h14a2 2 0 002-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 013 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
                          编辑
                        </button>
                      </div>
                    </div>
                  </div>
                </template>
                <!-- 批量删除结果卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'batch_delete_result')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'batch_delete_result')"
                    :key="'bdr-' + msg.id"
                    class="tool-card"
                    :class="card.success ? 'batch-result-success' : 'batch-result-partial'"
                  >
                    <div class="tool-card-hd">
                      <svg v-if="card.success" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="13" height="13"><path d="M5 13l4 4L19 7"/></svg>
                      <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="13" height="13"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
                      批量删除结果
                    </div>
                    <div class="batch-result-stats">
                      <span class="batch-stat-ok">✓ 成功 {{ card.deleted_count }} 条</span>
                      <span v-if="card.failed_count > 0" class="batch-stat-fail">✗ 失败 {{ card.failed_count }} 条</span>
                    </div>
                    <!-- 已删除列表 -->
                    <div v-if="Array.isArray(card.deleted_list) && card.deleted_list.length > 0" class="batch-result-section">
                      <div class="batch-result-section-title">已删除</div>
                      <div v-for="item in card.deleted_list" :key="item.id" class="batch-result-row batch-result-row-ok">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" width="10" height="10"><path d="M5 13l4 4L19 7"/></svg>
                        {{ item.title || item.id }}
                      </div>
                    </div>
                    <!-- 删除失败列表 -->
                    <div v-if="Array.isArray(card.failed_list) && card.failed_list.length > 0" class="batch-result-section">
                      <div class="batch-result-section-title">删除失败</div>
                      <div v-for="item in card.failed_list" :key="item.id" class="batch-result-row batch-result-row-fail">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="10" height="10"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                        {{ item.title || item.id }}
                      </div>
                    </div>
                  </div>
                </template>
                <!-- 维护模式操作结果卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'maintenance_result')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'maintenance_result')"
                    :key="'maint-' + msg.id"
                    class="tool-card maintenance-result-card"
                    :class="card.enabled ? 'maintenance-result-card--on' : 'maintenance-result-card--off'"
                  >
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="13" height="13"><circle cx="12" cy="12" r="3"/><path d="M19.07 4.93A10 10 0 1 0 4.93 19.07"/><path d="M16.24 7.76A6 6 0 1 0 7.76 16.24"/></svg>
                      {{ card.enabled ? '维护模式已开启' : '维护模式已关闭' }}
                    </div>
                    <div class="maintenance-status-badge" :class="card.enabled ? 'badge--on' : 'badge--off'">
                      <span class="maintenance-status-dot"></span>
                      {{ card.enabled ? '系统维护中' : '系统运行正常' }}
                    </div>
                    <div v-if="card.reason" class="maintenance-reason">
                      维护原因：{{ card.reason }}
                    </div>
                    <div class="maintenance-tip">
                      {{ card.enabled
                        ? '普通用户现在无法访问系统，请在维护完成后及时关闭维护模式。'
                        : '所有用户已恢复正常访问。' }}
                    </div>
                  </div>
                </template>
                <!-- 公告列表卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'announcement_list')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'announcement_list')"
                    :key="'ann-list-' + msg.id"
                    class="tool-card ann-list-card"
                  >
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="13" height="13"><path d="M18 8h1a4 4 0 010 8h-1"/><path d="M2 8h16v9a4 4 0 01-4 4H6a4 4 0 01-4-4V8z"/><line x1="6" y1="1" x2="6" y2="4"/><line x1="10" y1="1" x2="10" y2="4"/><line x1="14" y1="1" x2="14" y2="4"/></svg>
                      公告列表
                      <span class="tool-card-count">共 {{ card.total || 0 }} 条</span>
                    </div>
                    <div v-if="!Array.isArray(card.data) || card.data.length === 0" class="tool-card-empty">
                      暂无公告
                    </div>
                    <div
                      v-for="ann in (Array.isArray(card.data) ? card.data : [])"
                      :key="ann.id"
                      class="ann-item"
                    >
                      <div class="ann-item-hd">
                        <span v-if="ann.is_pinned" class="ann-pinned-tag">
                          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="10" height="10"><line x1="12" y1="17" x2="12" y2="22"/><path d="M5 17h14v-1.76a2 2 0 00-1.11-1.79l-1.78-.9A2 2 0 0115 10.76V6h1a2 2 0 000-4H8a2 2 0 000 4h1v4.76a2 2 0 01-1.11 1.79l-1.78.9A2 2 0 005 15.24V17z"/></svg>
                          置顶
                        </span>
                        <span class="ann-title">{{ ann.title }}</span>
                      </div>
                      <div class="ann-content">{{ ann.content }}</div>
                      <div class="ann-meta">
                        <span class="ann-time">{{ formatCardTime(ann.created_at) }}</span>
                        <span v-if="ann.expires_at && ann.expires_at > 0" class="ann-expires">
                          · 过期：{{ formatCardTime(ann.expires_at) }}
                        </span>
                        <span v-else class="ann-expires ann-expires--never">· 永不过期</span>
                      </div>
                    </div>
                    <!-- 分页提示 -->
                    <div v-if="card.total > (card.page_size || 10)" class="tool-card-empty" style="padding: 8px 0 0; font-size: 11px;">
                      共 {{ card.total }} 条，当前第 {{ card.page || 1 }} 页
                    </div>
                  </div>
                </template>

                <!-- 公告创建成功卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'announcement_created')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'announcement_created')"
                    :key="'ann-created-' + msg.id"
                    class="tool-card ann-created-card"
                  >
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="13" height="13"><polyline points="20 6 9 17 4 12"/></svg>
                      公告已发布
                    </div>
                    <div class="ann-created-title">{{ card.title }}</div>
                    <div class="ann-created-content">{{ card.content }}</div>
                    <div class="ann-created-meta">
                      <span v-if="card.is_pinned" class="ann-pinned-tag">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" width="10" height="10"><line x1="12" y1="17" x2="12" y2="22"/><path d="M5 17h14v-1.76a2 2 0 00-1.11-1.79l-1.78-.9A2 2 0 0115 10.76V6h1a2 2 0 000-4H8a2 2 0 000 4h1v4.76a2 2 0 01-1.11 1.79l-1.78.9A2 2 0 005 15.24V17z"/></svg>
                        置顶
                      </span>
                      <span class="ann-time">ID: {{ card.id }}</span>
                    </div>
                  </div>
                </template>

                <!-- 偏好洞察卡片 -->
                <template v-if="msg.toolCards.some(c => c.card_type === 'insights')">
                  <div
                    v-for="card in msg.toolCards.filter(c => c.card_type === 'insights')"
                    :key="'ins-' + msg.id"
                    class="tool-card insights-card"
                  >
                    <div class="tool-card-hd">
                      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" width="13" height="13"><path d="M18 20V10"/><path d="M12 20V4"/><path d="M6 20v-6"/></svg>
                      {{ Array.isArray(card.sections) && card.sections.length === 1
                          ? { keywords: '热门关键词', templates: '常用模板排行', pages: '页数分布', funnel: '用户留存漏斗' }[card.sections[0]] || '偏好洞察'
                          : '偏好洞察摘要' }}
                    </div>
                    <!-- 热门关键词词云 -->
                    <div v-if="card.data && card.data.top_topics && card.data.top_topics.length" class="insights-section">
                      <div v-if="!Array.isArray(card.sections) || card.sections.length > 1" class="insights-section-title">🔥 热门关键词</div>
                      <div class="insights-cloud">
                        <span
                          v-for="(tk, i) in card.data.top_topics.slice(0, 20)"
                          :key="i"
                          class="insights-cloud-word"
                          :style="{
                            fontSize: Math.round(11 + (card.data.top_topics[0].count > 0
                              ? (tk.count / card.data.top_topics[0].count) * 10
                              : 0)) + 'px',
                            opacity: Math.max(0.5, tk.count / (card.data.top_topics[0].count || 1)),
                            fontWeight: tk.count >= card.data.top_topics[0].count * 0.6 ? 700 : 500,
                            color: ['#4F46E5','#7C3AED','#0891B2','#059669','#D97706','#DC2626','#6366F1','#8B5CF6'][i % 8]
                          }"
                          :title="tk.keyword + '  ×' + tk.count"
                        >{{ tk.keyword }}</span>
                      </div>
                    </div>
                    <!-- 常用模板排行 -->
                    <div v-if="card.data && card.data.top_templates && card.data.top_templates.length" class="insights-section">
                      <div v-if="!Array.isArray(card.sections) || card.sections.length > 1" class="insights-section-title">🎨 常用模板 Top5</div>
                      <div class="insights-bar-list">
                        <div
                          v-for="(tpl, i) in card.data.top_templates.slice(0, 5)"
                          :key="i"
                          class="insights-bar-row"
                        >
                          <span class="insights-bar-label">{{ tpl.name }}</span>
                          <div class="insights-bar-track">
                            <div
                              class="insights-bar-fill"
                              :style="{ width: Math.max(4, Math.round((tpl.count / Math.max(...card.data.top_templates.map(t => t.count), 1)) * 100)) + '%' }"
                            ></div>
                          </div>
                          <span class="insights-bar-cnt">{{ tpl.count }}</span>
                        </div>
                      </div>
                    </div>
                    <!-- 页数分布 -->
                    <div v-if="card.data && card.data.pages_dist && card.data.pages_dist.length" class="insights-section">
                      <div v-if="!Array.isArray(card.sections) || card.sections.length > 1" class="insights-section-title">📄 页数分布</div>
                      <div class="insights-pages">
                        <div
                          v-for="(pg, i) in card.data.pages_dist"
                          :key="i"
                          class="insights-page-cell"
                        >
                          <div class="insights-page-val">{{ pg.count }}</div>
                          <div class="insights-page-label">{{ pg.label }}</div>
                        </div>
                      </div>
                    </div>
                    <!-- 用户留存漏斗 -->
                    <div v-if="card.data && card.data.funnel" class="insights-section">
                      <div v-if="!Array.isArray(card.sections) || card.sections.length > 1" class="insights-section-title">👥 用户留存漏斗</div>
                      <div class="insights-funnel">
                        <div class="insights-funnel-step">
                          <span class="insights-funnel-num">{{ card.data.funnel.registered }}</span>
                          <span class="insights-funnel-label">注册用户</span>
                        </div>
                        <svg viewBox="0 0 16 16" width="12" height="12" fill="none" stroke="#aaa" stroke-width="2"><path d="M8 2v12M4 10l4 4 4-4"/></svg>
                        <div class="insights-funnel-step">
                          <span class="insights-funnel-num">{{ card.data.funnel.generated_once }}</span>
                          <span class="insights-funnel-label">生成过PPT</span>
                        </div>
                        <svg viewBox="0 0 16 16" width="12" height="12" fill="none" stroke="#aaa" stroke-width="2"><path d="M8 2v12M4 10l4 4 4-4"/></svg>
                        <div class="insights-funnel-step">
                          <span class="insights-funnel-num">{{ card.data.funnel.generated_multi }}</span>
                          <span class="insights-funnel-label">高频用户(≥3次)</span>
                        </div>
                      </div>
                    </div>
                  </div>
                </template>
              </div>
              <div class="msg-time">{{ msg.time }}</div>
            </div>
          </div>
        </TransitionGroup>

        <!-- 打字动画 -->
        <Transition name="msg-fade">
          <div v-if="isLoading" class="msg-row msg-row-ai typing-row">
            <div class="msg-ai-avatar">
              <svg width="20" height="20" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
                <circle cx="32" cy="29" r="15" fill="#171717"/>
                <circle cx="32" cy="29" r="14" fill="#F5F5F5"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <path d="M27 37 Q32 41 37 37" stroke="#404040" stroke-width="1.5" stroke-linecap="round" fill="none"/>
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

        <!-- PPT 自动生成进度卡片（SSE 流式实时进度 · P4.1） -->
        <Transition name="msg-fade">
          <div v-if="genProgress && !genProgress.done && !genProgress.failed" class="msg-row msg-row-ai">
            <div class="msg-ai-avatar">
              <svg width="20" height="20" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
                <circle cx="32" cy="29" r="15" fill="#171717"/>
                <circle cx="32" cy="29" r="14" fill="#F5F5F5"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <path d="M27 37 Q32 41 37 37" stroke="#404040" stroke-width="1.5" stroke-linecap="round" fill="none"/>
              </svg>
            </div>
            <div class="msg-bubble-wrap">
              <div class="msg-bubble gen-progress-card">
                <div class="gen-progress-hd">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="14" height="14" class="spin-icon"><path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83"/></svg>
                  正在生成PPT…
                  <!-- SSE 实时标记 -->
                  <span class="gen-sse-badge">
                    <span class="gen-sse-dot"></span>实时
                  </span>
                </div>
                <div class="gen-progress-stage">{{ genProgress.stage }}{{ genProgress.step ? ' · ' + genProgress.step : '' }}</div>
                <!-- 阶段步骤指示器 -->
                <div class="gen-steps">
                  <div
                    v-for="(s, i) in genSteps"
                    :key="s.key"
                    class="gen-step"
                    :class="{
                      'gen-step-done':   genProgress.progress > s.threshold,
                      'gen-step-active': genProgress.progress >= s.threshold && genProgress.progress <= (genSteps[i+1]?.threshold ?? 101),
                    }"
                  >
                    <div class="gen-step-dot"></div>
                    <div class="gen-step-label">{{ s.label }}</div>
                  </div>
                </div>
                <div class="gen-progress-bar-wrap">
                  <div class="gen-progress-bar" :style="{ width: genProgress.progress + '%' }">
                    <div class="gen-progress-shimmer"></div>
                  </div>
                </div>
                <div class="gen-progress-pct">{{ genProgress.progress }}%</div>
              </div>
            </div>
          </div>
        </Transition>

        <!-- PPT 生成失败卡片 -->
        <Transition name="msg-fade">
          <div v-if="genProgress && genProgress.failed" class="msg-row msg-row-ai">
            <div class="msg-ai-avatar">
              <svg width="20" height="20" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
                <circle cx="32" cy="29" r="15" fill="#171717"/>
                <circle cx="32" cy="29" r="14" fill="#F5F5F5"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <path d="M27 37 Q32 41 37 37" stroke="#404040" stroke-width="1.5" stroke-linecap="round" fill="none"/>
              </svg>
            </div>
            <div class="msg-bubble-wrap">
              <div class="msg-bubble gen-failed-card">
                <div class="gen-failed-hd">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" width="15" height="15"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
                  PPT 生成失败
                </div>
                <div class="gen-failed-reason">{{ genProgress.step || '生成过程中发生错误，请重试' }}</div>
                <div class="gen-done-actions" style="margin-top:10px">
                  <button class="card-btn" @click="router.push('/main/generate')">重新生成</button>
                  <button class="card-btn card-btn-ghost" @click="$store.commit('setAssistantGenProgress', null)">关闭</button>
                </div>
              </div>
            </div>
          </div>
        </Transition>

        <!-- PPT 生成完成卡片 -->
        <Transition name="msg-fade">
          <div v-if="genProgress && genProgress.done" class="msg-row msg-row-ai">
            <div class="msg-ai-avatar">
              <svg width="20" height="20" viewBox="0 0 64 64" fill="none" xmlns="http://www.w3.org/2000/svg" style="display:block">
                <circle cx="32" cy="29" r="15" fill="#171717"/>
                <circle cx="32" cy="29" r="14" fill="#F5F5F5"/>
                <ellipse cx="26" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <ellipse cx="38" cy="27" rx="3.5" ry="4" fill="#171717"/>
                <circle cx="27.5" cy="25.5" r="1.2" fill="white"/>
                <circle cx="39.5" cy="25.5" r="1.2" fill="white"/>
                <path d="M27 37 Q32 41 37 37" stroke="#404040" stroke-width="1.5" stroke-linecap="round" fill="none"/>
              </svg>
            </div>
            <div class="msg-bubble-wrap">
              <div class="msg-bubble gen-done-card">
                <div class="gen-done-hd">
                  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" width="15" height="15"><path d="M5 13l4 4L19 7"/></svg>
                  PPT 生成完成！
                </div>
                <div class="gen-done-title">{{ genProgress.title }}</div>
                <div class="gen-done-actions">
                  <button class="card-btn" @click="openDownload(genProgress.pptId)">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="11" height="11"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
                    下载
                  </button>
                  <button class="card-btn" @click="router.push(`/main/edit/${genProgress.pptId}`)">
                    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="11" height="11"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
                    编辑
                  </button>
                  <button class="card-btn card-btn-ghost" @click="$store.commit('setAssistantGenProgress', null)">关闭</button>
                </div>
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
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="18" height="18">
              <line x1="22" y1="2" x2="11" y2="13"/>
              <polygon points="22 2 15 22 11 13 2 9 22 2"/>
            </svg>
          </button>
        </div>
        <div class="footer-hint">Shift+Enter 换行</div>
      </div>
    </div>
  </Transition>

  <!-- 操作确认弹窗（兼容新版 tool 和旧版 intent 两种结构） -->
  <el-dialog
    v-model="confirmVisible"
    :title="confirmDialogTitle"
    width="420px"
    :close-on-click-modal="false"
    :class="['tutu-confirm-dialog', isHighDangerConfirm ? 'tutu-confirm-dialog--danger' : '']"
    append-to-body
    @close="onConfirmClose"
  >
    <div class="confirm-body">
      <div class="confirm-icon-wrap">
        <!-- 高危操作：红色警告图标 -->
        <svg v-if="isHighDangerConfirm" viewBox="0 0 48 48" fill="none" width="44" height="44">
          <circle cx="24" cy="24" r="24" fill="#FEF2F2"/>
          <path d="M24 14v13M24 31v2" stroke="#EF4444" stroke-width="3" stroke-linecap="round"/>
        </svg>
        <!-- 普通操作：蓝色信息图标 -->
        <svg v-else viewBox="0 0 48 48" fill="none" width="44" height="44">
          <circle cx="24" cy="24" r="24" fill="#EFF6FF"/>
          <path d="M24 14v12M24 30v2" stroke="#0EA5E9" stroke-width="3" stroke-linecap="round"/>
        </svg>
      </div>
      <p :class="['confirm-desc', isHighDangerConfirm ? 'confirm-desc--danger' : '']">{{ pendingAction?.confirmText }}</p>
      <!-- 高危操作：CONFIRM 输入框 -->
      <div v-if="isHighDangerConfirm" class="confirm-code-wrap">
        <label class="confirm-code-label">请在下方输入 <code class="confirm-code-keyword">CONFIRM</code> 以确认</label>
        <input
          v-model="confirmCode"
          class="confirm-code-input"
          :class="{ 'confirm-code-input--error': confirmCodeError }"
          placeholder="输入 CONFIRM"
          autocomplete="off"
          spellcheck="false"
          @keyup.enter="onConfirmAction"
        />
        <p v-if="confirmCodeError" class="confirm-code-error">{{ confirmCodeError }}</p>
      </div>
      <div v-if="pendingAction?.params && Object.keys(pendingAction.params).length" class="confirm-meta">
        <!-- 新版 Tool Call 结构：通用参数展示 -->
        <template v-if="pendingAction.tool">
          <!-- 批量删除 PPT：展示被删清单 -->
          <template v-if="pendingAction.tool === 'batch_delete_ppt' && Array.isArray(pendingAction.params.ppt_list)">
            <div class="batch-confirm-list">
              <div class="batch-confirm-list-hd">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="12" height="12"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6"/></svg>
                以下 {{ pendingAction.params.ppt_list.length }} 个 PPT 将被永久删除：
              </div>
              <div class="batch-confirm-items">
                <div
                  v-for="(item, i) in pendingAction.params.ppt_list"
                  :key="item.id || i"
                  class="batch-confirm-item"
                >
                  <span class="batch-confirm-index">{{ i + 1 }}</span>
                  <div class="batch-confirm-item-body">
                    <div class="batch-confirm-title">{{ item.title || item.topic || '未命名' }}</div>
                    <div class="batch-confirm-meta">
                      <span v-if="item.pages">{{ item.pages }} 页</span>
                      <span v-if="item.pages && (item.template_name || item.created_at)" class="batch-confirm-dot">·</span>
                      <span v-if="item.template_name" class="batch-confirm-tpl">{{ item.template_name }}</span>
                      <span v-if="item.template_name && item.created_at" class="batch-confirm-dot">·</span>
                      <span v-if="item.created_at">{{ formatCardTime(item.created_at) }}</span>
                      <span v-if="!item.has_file" class="batch-confirm-no-file">无文件</span>
                    </div>
                    <div v-if="item.topic && item.topic !== item.title" class="batch-confirm-topic">{{ item.topic }}</div>
                  </div>
                </div>
              </div>
            </div>
          </template>
          <div v-else-if="pendingAction.params.ppt_title" class="meta-row">
            <span class="meta-key">操作对象</span>
            <span class="meta-val">{{ pendingAction.params.ppt_title }}</span>
          </div>
          <div v-if="pendingAction.params.topic" class="meta-row">
            <span class="meta-key">主题</span>
            <span class="meta-val">{{ pendingAction.params.topic }}</span>
          </div>
          <div v-if="pendingAction.params.page_count" class="meta-row">
            <span class="meta-key">页数</span>
            <span class="meta-val">{{ pendingAction.params.page_count }} 页</span>
          </div>
          <div v-if="pendingAction.params.style" class="meta-row">
            <span class="meta-key">风格</span>
            <span class="meta-val">{{ pendingAction.params.style }}</span>
          </div>
        </template>
        <!-- 旧版 intent 结构（向后兼容） -->
        <template v-else>
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
        </template>
      </div>
    </div>
    <template #footer>
      <div class="confirm-footer">
        <el-button @click="onCancelAction">取消</el-button>
        <el-button
          :type="isHighDangerConfirm ? 'danger' : 'primary'"
          :loading="actionLoading"
          :disabled="isHighDangerConfirm && confirmCode !== 'CONFIRM'"
          @click="onConfirmAction"
        >{{ isHighDangerConfirm ? '确认执行危险操作' : '确认执行' }}</el-button>
      </div>
    </template>
  </el-dialog>
</template>

<script setup>
import { ref, computed, watch, nextTick } from 'vue'
import { useStore } from 'vuex'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { marked } from 'marked'
import DOMPurify from 'dompurify'

// 配置 marked：安全且轻量
marked.setOptions({
  breaks: true,
  gfm: true,
})

function renderMarkdown(text) {
  if (!text) return ''
  const raw = marked.parse(text)
  return DOMPurify.sanitize(raw, {
    ALLOWED_TAGS: ['p', 'br', 'strong', 'em', 'del', 'code', 'pre', 'ul', 'ol', 'li', 'blockquote', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'a', 'table', 'thead', 'tbody', 'tr', 'th', 'td', 'hr', 'span'],
    ALLOWED_ATTR: ['href', 'target', 'rel', 'class'],
  })
}

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
const showHistory  = ref(false)     // P4.6 操作历史面板
const confirmCode       = ref('')   // 高危操作：用户输入的 CONFIRM 字符串
const confirmCodeError  = ref('')   // 高危确认码错误提示

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
const isAdmin            = computed(() => !!store.getters['currentUser']?.isAdmin)
const genProgress        = computed(() => store.state.assistantGenProgress)
const operationHistory   = computed(() => store.getters['assistant/operationHistory'])

// 生成阶段步骤（用于进度卡片的步骤指示器）
const genSteps = [
  { key: 'outline',   label: '生成大纲', threshold: 15 },
  { key: 'layout',    label: '分析版式', threshold: 30 },
  { key: 'content',   label: '生成内容', threshold: 45 },
  { key: 'images',    label: '配图生成', threshold: 60 },
  { key: 'rendering', label: '渲染文件', threshold: 80 },
  { key: 'done',      label: '完成',     threshold: 95 },
]

// ── 高危弹窗判断（需输入 CONFIRM 字符串）────────────────
const isHighDangerConfirm = computed(() => !!pendingAction.value?.requireConfirmCode)

// ── 确认弹窗标题（兼容新版 tool 和旧版 intent）────────
const confirmDialogTitle = computed(() => {
  const action = pendingAction.value
  if (!action) return '操作确认'
  // 新版 Tool Call 结构
  if (action.tool) {
    const toolMap = {
      delete_ppt:             '确认删除 PPT',
      batch_delete_ppt:       `确认批量删除 ${Array.isArray(action.params?.ppt_list) ? action.params.ppt_list.length + ' 个 ' : ''}PPT`,
      delete_material:        '确认删除素材',
      trigger_generate_ppt:   '确认生成 PPT',
      open_ppt_editor:        '打开 PPT 编辑器',
      download_ppt:           '确认下载 PPT',
      fill_login_form:        '跳转登录',
      toggle_maintenance_mode: action.params?.action === 'enable' ? '⚠️ 开启系统维护模式' : '确认关闭维护模式',
    }
    return toolMap[action.tool] || '操作确认'
  }
  // 旧版 intent 结构（向后兼容）
  const intentMap = {
    VIEW_PPT:      '打开 PPT 编辑器',
    DELETE_PPT:    '确认删除 PPT',
    GENERATE_PPT:  '确认生成 PPT',
    NAVIGATE:      '确认跳转',
    DOWNLOAD_PPT:  '确认下载 PPT',
    LIST_TEMPLATES:'查看模板'
  }
  return intentMap[action.intent] || '操作确认'
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
watch(pendingAction, (val) => {
  if (val) {
    confirmCode.value = ''
    confirmCodeError.value = ''
    confirmVisible.value = true
  }
})
watch(messages, () => { nextTick(scrollToBottom) }, { deep: true })
watch(isLoading, () => { nextTick(scrollToBottom) })
watch(genProgress, () => {
  nextTick(scrollToBottom)
})

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
    showHistory.value = false
    store.dispatch('assistant/fetchSessions')
  }
}

function toggleHistory() {
  showHistory.value = !showHistory.value
  if (showHistory.value) showSessions.value = false
}

function clearOperationHistory() {
  store.commit('assistant/clearOperationHistory')
}

async function handleNewSession() {
  const sessionId = await store.dispatch('assistant/newSession')
  if (sessionId) {
    showSessions.value = false
    ElMessage.success('已新建会话')
  } else {
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
    // 删除后刷新会话列表
    await store.dispatch('assistant/fetchSessions')
  } catch (err) {
    const msg = err?.response?.data?.message || '删除失败，请稍后重试'
    ElMessage.error(msg)
  }
}

async function handleDeleteCurrentSession() {
  const sessionId = currentSessionId.value
  if (!sessionId) return
  try {
    await store.dispatch('assistant/removeSession', sessionId)
    ElMessage.success('当前会话已删除')
    await store.dispatch('assistant/fetchSessions')
  } catch (err) {
    const msg = err?.response?.data?.message || '删除失败，请稍后重试'
    ElMessage.error(msg)
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
  // 传入 router 供 Tool Executor 使用
  await store.dispatch('assistant/chat', { message: text, router })
}

function sendExample(text) { inputText.value = text; sendMessage() }

function autoResize(e) {
  const el = e.target
  el.style.height = 'auto'
  el.style.height = Math.min(el.scrollHeight, 120) + 'px'
}

// ── 确认/取消（兼容新版 Tool Call 和旧版 intent 两种结构）──────────────────
async function onConfirmAction() {
  // 高危操作：校验 CONFIRM 输入码
  if (isHighDangerConfirm.value) {
    if (confirmCode.value !== 'CONFIRM') {
      confirmCodeError.value = '请输入大写的 CONFIRM 以确认执行此危险操作'
      return
    }
    confirmCodeError.value = ''
    actionLoading.value = true
    try {
      await store.dispatch('assistant/confirmActionWithCode', { router, confirmCode: confirmCode.value })
    } finally {
      actionLoading.value = false
      confirmVisible.value = false
      confirmCode.value = ''
    }
    return
  }
  // 普通确认
  actionLoading.value = true
  try {
    await store.dispatch('assistant/confirmAction', router)
  } finally {
    actionLoading.value = false
    confirmVisible.value = false
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

// ── 工具卡片辅助函数 ──────────────────────────────────
function formatCardTime(ts) {
  if (!ts) return ''
  try {
    const d = typeof ts === 'number' ? new Date(ts * 1000) : new Date(ts)
    return d.toLocaleDateString('zh-CN', { month: 'numeric', day: 'numeric' })
  } catch { return '' }
}

function formatFileSize(bytes) {
  if (!bytes) return ''
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB'
  return (bytes / 1024 / 1024).toFixed(1) + ' MB'
}

function statusLabel(status) {
  const map = {
    completed:  '已完成',
    generating: '生成中',
    pending:    '等待中',
    failed:     '失败',
    extracting: '提取中',
  }
  return map[status] || status || ''
}

// PPT 缩略图根据 style 字段显示不同背景色
function pptThumbClass(ppt) {
  const styleMap = {
    business:   'ppt-thumb-blue',
    tech:       'ppt-thumb-indigo',
    creative:   'ppt-thumb-violet',
    education:  'ppt-thumb-green',
    minimal:    'ppt-thumb-gray',
    nature:     'ppt-thumb-teal',
    dark:       'ppt-thumb-dark',
  }
  return styleMap[ppt.style] || 'ppt-thumb-blue'
}

// 带 token 下载 PPT 文件
async function openDownload(pptId) {
  if (!pptId) return
  const token = localStorage.getItem('token') || sessionStorage.getItem('token')
  if (!token) {
    ElMessage.error('请先登录')
    return
  }
  try {
    const resp = await fetch(`/api/ppt/file?id=${encodeURIComponent(pptId)}`, {
      headers: { Authorization: `Bearer ${token}` }
    })
    if (!resp.ok) {
      const text = await resp.text()
      ElMessage.error('下载失败：' + text)
      return
    }
    const blob = await resp.blob()
    const blobUrl = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = blobUrl
    a.download = `ppt_${pptId}.pptx`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(blobUrl)
  } catch (e) {
    ElMessage.error('下载失败：' + e.message)
  }
}

// 从卡片直接触发素材删除（发送到助手 chat 流程，由 LLM 确认）
async function askDeleteMaterial(mat) {
  const text = `删除素材「${mat.filename}」`
  inputText.value = text
  await sendMessage()
}

// 管理员：上架/下架模板
async function askToggleTemplate(tmpl, action) {
  const actionLabel = action === 'activate' ? '上架' : '下架'
  inputText.value = `将模板「${tmpl.name}」（ID: ${tmpl.id}）${actionLabel}`
  await nextTick()
  sendMessage()
}

// 预览素材：跳转到素材页并打开指定素材的预览弹窗
async function previewMaterial(mat) {
  if (!mat?.id) return
  if (router) await router.push({ path: '/admin', query: { nav: 'materials', preview: mat.id } })
}

// 管理员：触发 AI 内容审核
async function askReviewMaterial(mat) {
  const text = `审核素材「${mat.filename}」（ID: ${mat.id}）`
  inputText.value = text
  await sendMessage()
}

// 管理员：强制删除素材（需输入原因）
async function askAdminDeleteMaterial(mat) {
  const reason = window.prompt(`请输入删除素材「${mat.filename}」的原因（将通知用户）：`)
  if (reason === null) return  // 用户取消
  const text = `强制删除素材「${mat.filename}」（ID: ${mat.id}），原因：${reason || '违规内容'}`
  inputText.value = text
  await sendMessage()
}
</script>

<style scoped>
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap');

/* ── 全局与容器字体 ─────────────────────────────────── */
.ai-panel, .ai-trigger, .tutu-confirm-dialog {
  font-family: 'Inter', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
}

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
  transition: transform 0.3s cubic-bezier(0.2, 0.8, 0.2, 1);
}

.trigger-ring {
  position: absolute;
  inset: -6px;
  border-radius: 50%;
  border: 1px solid rgba(23, 23, 23, 0.1);
  background: transparent;
  pointer-events: none;
  transition: all 0.3s ease;
}

.trigger-ring.has-unread {
  border: 1.5px solid rgba(212, 175, 55, 0.8);
  box-shadow: 0 0 15px rgba(212, 175, 55, 0.2);
  animation: ring-pulse-gold 2s infinite;
}

@keyframes ring-pulse-gold {
  0% { transform: scale(0.95); opacity: 0.8; }
  50% { transform: scale(1.05); opacity: 0.3; }
  100% { transform: scale(0.95); opacity: 0.8; }
}

.ai-trigger.is-open .trigger-ring,
.ai-trigger.is-dragging .trigger-ring {
  opacity: 0;
  animation: none;
}

.trigger-avatar {
  width: 56px;
  height: 56px;
  border-radius: 50%;
  background: #171717; /* 深空黑 */
  box-shadow: 
    0 8px 20px -4px rgba(0, 0, 0, 0.3),
    inset 0 1px 1px rgba(255, 255, 255, 0.1);
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  position: relative;
  z-index: 1;
  transition: all 0.3s cubic-bezier(0.2, 0.8, 0.2, 1);
  border: 1px solid rgba(255, 255, 255, 0.05);
}

.ai-trigger:hover .trigger-avatar {
  transform: translateY(-2px);
  box-shadow: 
    0 12px 24px -4px rgba(0, 0, 0, 0.4),
    0 0 0 4px rgba(23, 23, 23, 0.05);
}

.ai-trigger.is-open .trigger-avatar {
  transform: scale(0.92);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
}

.ai-trigger.is-dragging .trigger-avatar {
  transform: scale(1.05);
  box-shadow: 0 16px 32px rgba(0, 0, 0, 0.4);
  cursor: grabbing;
}

.unread-badge {
  position: absolute;
  top: 0;
  right: 0;
  background: #171717;
  color: #D4AF37; /* Accent Gold */
  font-size: 11px;
  font-weight: 700;
  min-width: 20px;
  height: 20px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0 5px;
  border: 2px solid white;
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.15);
  z-index: 2;
  animation: badge-pop 0.4s cubic-bezier(0.34, 1.56, 0.64, 1);
}

@keyframes badge-pop {
  from { transform: scale(0); }
  to   { transform: scale(1); }
}

/* ── 对话面板 ─────────────────────────────────────── */
.ai-panel {
  position: fixed;
  z-index: 8001;
  width: 400px;
  height: 620px;
  background: #FFFFFF;
  border: 1px solid #E5E5E5;
  border-radius: 16px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  box-shadow: 
    0 20px 40px -10px rgba(0, 0, 0, 0.1),
    0 0 10px rgba(0, 0, 0, 0.02);
}

.panel-anim-enter-active { transition: all 0.3s cubic-bezier(0.2, 0.8, 0.2, 1); }
.panel-anim-leave-active { transition: all 0.2s ease-in; }
.panel-anim-enter-from   { opacity: 0; transform: translateY(15px); }
.panel-anim-leave-to     { opacity: 0; transform: translateY(10px); }

/* ── 面板头部 ─────────────────────────────────────── */
.panel-header {
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 20px;
  background: #FAFAFA;
  border-bottom: 1px solid #E5E5E5;
  position: relative;
  overflow: hidden;
}

.panel-header-orb { display: none; } /* 极简风格不需要光晕 */

.panel-header-inner {
  display: flex;
  align-items: center;
  gap: 12px;
}

.panel-hd-avatar {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: #171717;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  border: 1px solid rgba(0,0,0,0.05);
}

.avatar-online-dot {
  position: absolute;
  bottom: -2px;
  right: -2px;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #10B981;
  border: 2px solid #FAFAFA;
}

.panel-hd-info {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.panel-hd-name {
  font-size: 15px;
  font-weight: 600;
  color: #171717;
  line-height: 1.2;
}

.panel-hd-status {
  font-size: 12px;
  color: #737373;
  display: flex;
  align-items: center;
  gap: 6px;
  font-weight: 500;
}

.status-pulse {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #10B981;
  opacity: 0.8;
}

.panel-hd-actions {
  display: flex;
  align-items: center;
  gap: 6px;
}

.hd-btn {
  width: 32px;
  height: 32px;
  border: 1px solid transparent;
  background: transparent;
  border-radius: 8px;
  color: #737373;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s ease;
}

.hd-btn:hover {
  background: #F5F5F5;
  color: #171717;
  border-color: #E5E5E5;
}

.hd-btn:active { transform: scale(0.96); }

.hd-btn-close:hover { 
  background: #FEE2E2; 
  color: #EF4444; 
  border-color: #FECACA;
}

.hd-btn-danger:hover { 
  background: #FEE2E2; 
  color: #EF4444; 
  border-color: #FECACA;
}

.hd-btn-active {
  background: #E5E5E5 !important;
  color: #171717 !important;
  border-color: #D4D4D4 !important;
}

/* ── 会话列表侧边栏 ───────────────────────────────── */
.sessions-sidebar {
  position: absolute;
  top: 69px; /* 头部高度 */
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(255, 255, 255, 0.98);
  backdrop-filter: blur(12px);
  z-index: 10;
  display: flex;
  flex-direction: column;
  border-top: 1px solid #E5E5E5;
}

.sessions-slide-enter-active, .sessions-slide-leave-active { transition: all 0.3s cubic-bezier(0.2, 0.8, 0.2, 1); }
.sessions-slide-enter-from, .sessions-slide-leave-to { transform: translateY(-10px); opacity: 0; }

.sessions-hd {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 20px;
  border-bottom: 1px solid #F5F5F5;
}

.sessions-hd-title {
  font-size: 13px;
  font-weight: 600;
  color: #404040;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.sessions-loading {
  font-size: 12px;
  color: #A3A3A3;
}

.sessions-list {
  flex: 1;
  overflow-y: auto;
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.sessions-list::-webkit-scrollbar { width: 4px; }
.sessions-list::-webkit-scrollbar-thumb { background: #E5E5E5; border-radius: 4px; }

.session-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  border-radius: 8px;
  cursor: pointer;
  background: transparent;
  border: 1px solid transparent;
  transition: all 0.2s ease;
}

.session-item:hover {
  background: #FAFAFA;
  border-color: #F5F5F5;
}

.session-item.is-active {
  background: #F5F5F5;
  border-color: #E5E5E5;
  border-left: 3px solid #171717;
  padding-left: 14px;
}

.session-item-icon {
  color: #A3A3A3;
  display: flex;
  align-items: center;
}

.session-item.is-active .session-item-icon { color: #171717; }

.session-item-body { flex: 1; min-width: 0; }

.session-item-title {
  font-size: 14px;
  font-weight: 500;
  color: #404040;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  margin-bottom: 2px;
}

.session-item.is-active .session-item-title { color: #171717; font-weight: 600; }

.session-item-time {
  font-size: 11px;
  color: #A3A3A3;
}

.session-del-btn {
  width: 28px;
  height: 28px;
  border: none;
  background: transparent;
  border-radius: 6px;
  color: #A3A3A3;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  opacity: 0;
  transition: all 0.2s;
}

.session-item:hover .session-del-btn { opacity: 1; }
.session-del-btn:hover { background: #FEE2E2; color: #EF4444; }

.sessions-empty {
  text-align: center;
  padding: 40px 20px;
  font-size: 13px;
  color: #A3A3A3;
}

/* ── 操作历史时间线（P4.6）──────────────────────────── */

/* 头部操作按钮：带小圆点提示 */
.hd-btn-history {
  position: relative;
}
.hd-btn-history-dot {
  position: absolute;
  top: 5px;
  right: 5px;
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #F59E0B;
  border: 1.5px solid #FAFAFA;
}

/* 历史侧边栏复用 sessions-sidebar 布局，仅覆盖特化部分 */
.op-history-sidebar .sessions-hd {
  justify-content: space-between;
}

.op-history-clear-btn {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11.5px;
  color: #A3A3A3;
  background: none;
  border: 1px solid #E5E5E5;
  border-radius: 6px;
  padding: 3px 8px;
  cursor: pointer;
  transition: all 0.18s ease;
  line-height: 1.4;
}
.op-history-clear-btn:hover:not(:disabled) {
  color: #EF4444;
  border-color: #FECACA;
  background: #FEF2F2;
}
.op-history-clear-btn:disabled {
  opacity: 0.4;
  cursor: default;
}

.op-history-list {
  padding: 16px 14px 16px 16px;
}

/* 时间线容器 */
.op-timeline {
  display: flex;
  flex-direction: column;
  gap: 0;
}

/* 单条操作记录 */
.op-item {
  display: flex;
  gap: 0;
  align-items: flex-start;
}

/* 竖线 + 圆点列 */
.op-dot-col {
  display: flex;
  flex-direction: column;
  align-items: center;
  width: 24px;
  flex-shrink: 0;
  margin-right: 10px;
}

.op-dot {
  width: 22px;
  height: 22px;
  border-radius: 50%;
  border: 1.5px solid #E5E5E5;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  background: #FAFAFA;
  transition: background 0.18s;
}

.op-item--success .op-dot {
  background: #D1FAE5;
  border-color: #6EE7B7;
  color: #059669;
}
.op-item--cancelled .op-dot {
  background: #F5F5F5;
  border-color: #D4D4D4;
  color: #A3A3A3;
}
.op-item--error .op-dot {
  background: #FEE2E2;
  border-color: #FCA5A5;
  color: #EF4444;
}

/* 时间线竖线 */
.op-line {
  width: 1.5px;
  flex: 1;
  min-height: 14px;
  background: #E5E5E5;
  margin: 3px 0;
}

/* 操作文字内容 */
.op-content {
  flex: 1;
  min-width: 0;
  padding-bottom: 16px;
}

.op-label {
  font-size: 13px;
  color: #404040;
  line-height: 1.5;
  word-break: break-all;
  margin-bottom: 4px;
  padding-top: 2px;
}

.op-meta {
  display: flex;
  align-items: center;
  gap: 6px;
}

.op-status-tag {
  font-size: 10.5px;
  font-weight: 600;
  padding: 1px 6px;
  border-radius: 10px;
  line-height: 1.6;
}
.op-status-tag--success  { background: #D1FAE5; color: #059669; }
.op-status-tag--cancelled { background: #F5F5F5; color: #737373; }
.op-status-tag--error    { background: #FEE2E2; color: #EF4444; }

.op-time {
  font-size: 10.5px;
  color: #A3A3A3;
}

/* 新条目入场动画 */
.op-item-fade-enter-active { transition: all 0.3s ease; }
.op-item-fade-enter-from   { opacity: 0; transform: translateY(-6px); }

/* ── 消息区 ───────────────────────────────────────── */
.panel-body {
  flex: 1;
  overflow-y: auto;
  padding: 24px 20px;
  background: #FFFFFF;
  display: flex;
  flex-direction: column;
  gap: 0;
  scroll-behavior: smooth;
}

.panel-body::-webkit-scrollbar { width: 5px; }
.panel-body::-webkit-scrollbar-track { background: transparent; }
.panel-body::-webkit-scrollbar-thumb { background: #E5E5E5; border-radius: 4px; }
.panel-body::-webkit-scrollbar-thumb:hover { background: #D4D4D4; }

/* 欢迎屏 */
.welcome-screen {
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  margin-top: auto;
  margin-bottom: auto;
  gap: 16px;
}

.welcome-fade-enter-active { transition: all 0.4s cubic-bezier(0.2, 0.8, 0.2, 1); }
.welcome-fade-leave-active { transition: all 0.2s ease-in; }
.welcome-fade-enter-from   { opacity: 0; transform: translateY(10px); }
.welcome-fade-leave-to     { opacity: 0; }

.welcome-avatar-ring {
  width: 72px;
  height: 72px;
  border-radius: 50%;
  background: #171717;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 12px 24px -6px rgba(0, 0, 0, 0.15);
  border: 1px solid #E5E5E5;
}

.welcome-title {
  font-size: 20px;
  font-weight: 600;
  color: #171717;
  letter-spacing: -0.5px;
}

.welcome-title strong {
  color: #D4AF37; /* Accent Gold */
}

.welcome-sub {
  font-size: 14px;
  color: #737373;
  line-height: 1.6;
}

.welcome-chips {
  display: flex;
  flex-direction: column;
  gap: 8px;
  width: 100%;
  margin-top: 12px;
}

.chip {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  font-size: 13px;
  font-weight: 500;
  color: #404040;
  background: #FFFFFF;
  border: 1px solid #E5E5E5;
  border-radius: 8px;
  padding: 12px 16px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.chip:hover {
  background: #FAFAFA;
  border-color: #171717;
  color: #171717;
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.05);
}

.chip:active { transform: translateY(0); }

/* 消息列表 */
.msg-list {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding-bottom: 10px;
}

.msg-row {
  display: flex;
  align-items: flex-end;
  gap: 12px;
  max-width: 100%;
}

.msg-row-user {
  flex-direction: row-reverse;
}

.msg-ai-avatar {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  background: #171717;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  border: 1px solid rgba(0,0,0,0.1);
}

.msg-bubble-wrap {
  display: flex;
  flex-direction: column;
  gap: 6px;
  max-width: 280px;
}

.msg-row-user .msg-bubble-wrap { align-items: flex-end; }

.msg-bubble {
  padding: 12px 16px;
  border-radius: 12px;
  font-size: 14px;
  line-height: 1.6;
  word-break: break-word;
  white-space: pre-wrap;
}

.msg-row-ai .msg-bubble {
  background: #FAFAFA;
  color: #171717;
  border: 1px solid #E5E5E5;
  border-bottom-left-radius: 4px;
}

.msg-row-user .msg-bubble {
  background: #171717;
  color: #FFFFFF;
  border: 1px solid #171717;
  border-bottom-right-radius: 4px;
}

/* ── Markdown 富文本渲染样式 ────────────────────────── */
.msg-bubble-md {
  white-space: normal;  /* 覆盖 pre-wrap，由 marked 负责换行 */
}

.msg-bubble-md p {
  margin: 0 0 8px;
  line-height: 1.65;
}
.msg-bubble-md p:last-child { margin-bottom: 0; }

.msg-bubble-md h1,
.msg-bubble-md h2,
.msg-bubble-md h3,
.msg-bubble-md h4 {
  margin: 10px 0 6px;
  font-weight: 600;
  line-height: 1.4;
}
.msg-bubble-md h1 { font-size: 15px; }
.msg-bubble-md h2 { font-size: 14px; }
.msg-bubble-md h3,
.msg-bubble-md h4 { font-size: 13px; }

.msg-bubble-md ul,
.msg-bubble-md ol {
  margin: 4px 0 8px;
  padding-left: 18px;
}
.msg-bubble-md li { margin-bottom: 3px; line-height: 1.6; }

.msg-bubble-md strong { font-weight: 600; color: #111; }
.msg-bubble-md em    { font-style: italic; color: #555; }
.msg-bubble-md del   { text-decoration: line-through; color: #999; }

/* 行内代码 */
.msg-bubble-md code {
  font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace;
  font-size: 12.5px;
  background: rgba(0,0,0,0.06);
  color: #C7254E;
  padding: 1px 5px;
  border-radius: 4px;
  border: 1px solid rgba(0,0,0,0.08);
}

/* 代码块 */
.msg-bubble-md pre {
  margin: 8px 0;
  padding: 10px 14px;
  background: #1E1E2E;
  border-radius: 8px;
  overflow-x: auto;
}
.msg-bubble-md pre code {
  background: none;
  color: #CDD6F4;
  border: none;
  padding: 0;
  font-size: 12px;
  line-height: 1.6;
}

/* 引用块 */
.msg-bubble-md blockquote {
  margin: 6px 0;
  padding: 6px 12px;
  border-left: 3px solid #D4AF37;
  background: rgba(212,175,55,0.06);
  color: #555;
  border-radius: 0 6px 6px 0;
  font-style: italic;
}
.msg-bubble-md blockquote p { margin: 0; }

/* 分隔线 */
.msg-bubble-md hr {
  border: none;
  border-top: 1px solid #E5E5E5;
  margin: 8px 0;
}

/* 链接 */
.msg-bubble-md a {
  color: #2563EB;
  text-decoration: underline;
  text-underline-offset: 2px;
}
.msg-bubble-md a:hover { color: #1D4ED8; }

/* 表格 */
.msg-bubble-md table {
  border-collapse: collapse;
  width: 100%;
  margin: 8px 0;
  font-size: 12.5px;
}
.msg-bubble-md th,
.msg-bubble-md td {
  border: 1px solid #E5E5E5;
  padding: 5px 10px;
  text-align: left;
}
.msg-bubble-md th {
  background: #F5F5F5;
  font-weight: 600;
}
.msg-bubble-md tr:nth-child(even) td { background: #FAFAFA; }

.msg-time {
  font-size: 11px;
  color: #A3A3A3;
  padding: 0 2px;
}

/* 打字动画 */
.typing-row { margin-top: 8px; }

.typing-bubble {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 14px 18px;
  min-width: 50px;
}

.dot {
  width: 5px;
  height: 5px;
  border-radius: 50%;
  background: #A3A3A3;
  animation: typing-dot 1.4s infinite ease-in-out both;
}
.dot:nth-child(1) { animation-delay: -0.32s; }
.dot:nth-child(2) { animation-delay: -0.16s; }

@keyframes typing-dot {
  0%, 80%, 100% { transform: scale(0); opacity: 0.5; }
  40% { transform: scale(1); opacity: 1; background: #171717; }
}

.msg-fade-enter-active { transition: all 0.3s cubic-bezier(0.2, 0.8, 0.2, 1); }
.msg-fade-enter-from   { opacity: 0; transform: translateY(10px); }
.msg-fade-leave-active { transition: all 0.2s ease-in; }
.msg-fade-leave-to     { opacity: 0; }

/* ── 输入区 ───────────────────────────────────────── */
.panel-footer {
  flex-shrink: 0;
  padding: 16px 20px 24px;
  background: #FFFFFF;
  border-top: 1px solid #E5E5E5;
}

.input-wrapper {
  display: flex;
  align-items: flex-end;
  gap: 12px;
  background: #FAFAFA;
  border: 1px solid #E5E5E5;
  border-radius: 12px;
  padding: 12px 14px;
  transition: all 0.2s ease;
}

.input-wrapper.is-focused {
  border-color: #171717;
  background: #FFFFFF;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.04);
}

.input-wrapper.is-disabled { opacity: 0.6; }

.chat-input {
  flex: 1;
  border: none;
  background: transparent;
  padding: 2px 0;
  font-size: 14px;
  font-family: inherit;
  resize: none;
  outline: none;
  line-height: 1.5;
  max-height: 120px;
  overflow-y: auto;
  color: #171717;
}

.chat-input::placeholder { color: #A3A3A3; }
.chat-input:disabled { cursor: not-allowed; }

.send-btn {
  width: 32px;
  height: 32px;
  border: none;
  border-radius: 8px;
  background: #E5E5E5;
  color: #A3A3A3;
  cursor: not-allowed;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  transition: all 0.2s ease;
}

.send-btn.can-send {
  background: #171717;
  color: #FFFFFF;
  cursor: pointer;
}

.send-btn.can-send:hover {
  background: #404040;
  transform: translateY(-1px);
}

.send-btn.can-send:active { transform: translateY(0); }

.footer-hint {
  font-size: 11px;
  color: #A3A3A3;
  text-align: right;
  margin-top: 8px;
  font-weight: 500;
}

/* ── 确认弹窗内容 ─────────────────────────────────── */
.confirm-body {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 10px 0;
}

.confirm-icon-wrap {
  display: none; /* 极简风格不需要这个大图标 */
}

.confirm-desc {
  font-size: 16px;
  color: #171717;
  font-weight: 600;
  line-height: 1.5;
  text-align: left;
}

.confirm-meta {
  width: 100%;
  background: #FAFAFA;
  border: 1px solid #E5E5E5;
  border-radius: 8px;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  text-align: left;
}

.meta-row {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 16px;
  font-size: 14px;
  border-bottom: 1px dashed #E5E5E5;
  padding-bottom: 8px;
}
.meta-row:last-child {
  border-bottom: none;
  padding-bottom: 0;
}

.meta-key {
  color: #737373;
  font-weight: 500;
  flex-shrink: 0;
}

.meta-val {
  color: #171717;
  font-weight: 600;
  text-align: right;
  word-break: break-all;
}

.confirm-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  margin-top: 10px;
}

/* ── 工具结果卡片 ─────────────────────────────────── */
.tool-cards {
  margin-top: 8px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  width: 100%;
}

.tool-card {
  background: #FFFFFF;
  border: 1px solid #E5E5E5;
  border-radius: 8px;
  padding: 16px;
  font-size: 13px;
  box-shadow: 0 1px 3px rgba(0,0,0,0.02);
}

.tool-card-hd {
  display: flex;
  align-items: center;
  gap: 8px;
  font-weight: 600;
  color: #171717;
  margin-bottom: 12px;
  font-size: 13px;
}

.tool-card-hd svg { color: #737373; }

/* 批量下载卡片状态标签 */
.bdl-status {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  font-weight: 500;
  padding: 2px 7px;
  border-radius: 20px;
  margin-left: auto;
}
.bdl-status--packing {
  background: #FEF9C3;
  color: #854D0E;
}
.bdl-status--done {
  background: #DCFCE7;
  color: #166534;
}
.bdl-status--error {
  background: #FEE2E2;
  color: #991B1B;
}
@keyframes spin { to { transform: rotate(360deg); } }
.spin-icon { animation: spin 1s linear infinite; }
.ppt-meta-topic {
  color: #a3a3a3;
  font-style: italic;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 120px;
  display: inline-block;
  vertical-align: bottom;
}

.tool-card-item {
  padding: 12px 0;
  border-bottom: 1px solid #F5F5F5;
}

.tool-card-item:last-child {
  border-bottom: none;
  padding-bottom: 0;
}

.tool-card-item-title {
  font-weight: 600;
  color: #171717;
  font-size: 14px;
  line-height: 1.4;
  margin-bottom: 4px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tool-card-item-meta {
  font-size: 12px;
  color: #737373;
  margin-bottom: 10px;
}

.tool-card-item-actions {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.mat-review-reason {
  display: flex;
  align-items: flex-start;
  gap: 4px;
  font-size: 11px;
  color: #a3a3a3;
  margin-bottom: 8px;
  line-height: 1.4;
}
.mat-review-reason svg { flex-shrink: 0; margin-top: 2px; }

.card-btn {
  font-size: 12px;
  font-weight: 500;
  padding: 6px 14px;
  border-radius: 6px;
  border: 1px solid #E5E5E5;
  background: #FFFFFF;
  color: #171717;
  cursor: pointer;
  transition: all 0.2s;
}

.card-btn:hover {
  background: #FAFAFA;
  border-color: #D4D4D4;
}

.card-btn-dl {
  background: #171717;
  color: #FFFFFF;
  border-color: #171717;
}

.card-btn-dl:hover { 
  background: #404040;
  border-color: #404040;
}

.card-btn-danger {
  border-color: #FECACA;
  background: #FEF2F2;
  color: #DC2626;
}

.card-btn-danger:hover { background: #FEE2E2; border-color: #FCA5A5; }

.tool-card-count {
  margin-left: auto;
  font-size: 11px;
  font-weight: 500;
  color: #737373;
  background: #F5F5F5;
  padding: 2px 8px;
  border-radius: 10px;
}

.tool-card-badge {
  font-size: 11px;
  font-weight: 500;
  padding: 2px 8px;
  border-radius: 10px;
}

.badge-active { background: #ECFCCB; color: #059669; }
.badge-inactive { background: #F5F5F5; color: #737373; }
.badge-vector { background: #FEF3C7; color: #D97706; }
.badge-danger { background: #FEE2E2; color: #DC2626; }
.badge-pending { background: #F3F4F6; color: #6B7280; }

.card-btn-warn { background: #FFF7ED; color: #C2410C; border-color: #FED7AA; }
.card-btn-warn:hover { background: #FFEDD5; }
.tool-card-op-msg {
  display: flex; align-items: center; gap: 4px;
  font-size: 12px; color: #16A34A; padding: 4px 8px 6px;
  border-bottom: 1px solid #DCFCE7; margin-bottom: 4px;
}

/* ── PPT 生成进度卡片（P4.1 SSE 流式进度）── */
.gen-progress-card {
  min-width: 230px; max-width: 290px;
  background: #F8FAFC; border: 1px solid #E2E8F0;
  border-radius: 10px; padding: 12px 14px;
}
.gen-progress-hd {
  display: flex; align-items: center; gap: 6px;
  font-size: 13px; font-weight: 600; color: #1E293B; margin-bottom: 6px;
}
/* SSE 实时标记小徽章 */
.gen-sse-badge {
  margin-left: auto;
  display: flex; align-items: center; gap: 4px;
  font-size: 10px; font-weight: 500; color: #16A34A;
  background: #DCFCE7; border-radius: 10px;
  padding: 2px 7px;
}
.gen-sse-dot {
  width: 5px; height: 5px; border-radius: 50%;
  background: #16A34A;
  animation: sse-pulse 1.5s ease-in-out infinite;
}
@keyframes sse-pulse {
  0%, 100% { opacity: 1; transform: scale(1); }
  50%       { opacity: 0.5; transform: scale(0.7); }
}
.gen-progress-stage {
  font-size: 11px; color: #64748B; margin-bottom: 8px;
  white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
/* 阶段步骤指示器 */
.gen-steps {
  display: flex; align-items: center; gap: 0;
  margin-bottom: 10px;
}
.gen-step {
  flex: 1; display: flex; flex-direction: column;
  align-items: center; position: relative;
}
.gen-step:not(:last-child)::after {
  content: ''; position: absolute;
  top: 5px; left: 50%; width: 100%;
  height: 2px; background: #E2E8F0;
  z-index: 0;
}
.gen-step-done:not(:last-child)::after,
.gen-step-active:not(:last-child)::after {
  background: linear-gradient(90deg, #6366F1, #E2E8F0);
}
.gen-step-dot {
  width: 10px; height: 10px; border-radius: 50%;
  background: #E2E8F0; border: 2px solid #CBD5E1;
  z-index: 1; position: relative;
  transition: all 0.3s ease;
}
.gen-step-done .gen-step-dot {
  background: #6366F1; border-color: #6366F1;
}
.gen-step-active .gen-step-dot {
  background: #FFFFFF; border-color: #6366F1;
  box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.2);
  animation: step-pulse 1.5s ease-in-out infinite;
}
@keyframes step-pulse {
  0%, 100% { box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.2); }
  50%       { box-shadow: 0 0 0 5px rgba(99, 102, 241, 0.1); }
}
.gen-step-label {
  font-size: 9px; color: #94A3B8; margin-top: 3px;
  white-space: nowrap;
}
.gen-step-done .gen-step-label  { color: #6366F1; }
.gen-step-active .gen-step-label { color: #4338CA; font-weight: 600; }

.gen-progress-bar-wrap {
  height: 6px; background: #E2E8F0; border-radius: 3px; overflow: hidden; margin-bottom: 4px;
  position: relative;
}
.gen-progress-bar {
  height: 100%; background: linear-gradient(90deg, #6366F1, #8B5CF6);
  border-radius: 3px; transition: width 0.6s cubic-bezier(0.4, 0, 0.2, 1);
  position: relative; overflow: hidden;
}
/* 进度条流光效果 */
.gen-progress-shimmer {
  position: absolute; inset: 0;
  background: linear-gradient(90deg, transparent 0%, rgba(255,255,255,0.35) 50%, transparent 100%);
  animation: shimmer 1.8s ease-in-out infinite;
}
@keyframes shimmer {
  0%   { transform: translateX(-100%); }
  100% { transform: translateX(200%); }
}
.gen-progress-pct {
  font-size: 11px; color: #6366F1; font-weight: 600; text-align: right;
}

/* ── PPT 生成失败卡片 ── */
.gen-failed-card {
  min-width: 220px; max-width: 280px;
  background: #FFF1F2; border: 1px solid #FECDD3;
  border-radius: 10px; padding: 12px 14px;
}
.gen-failed-hd {
  display: flex; align-items: center; gap: 6px;
  font-size: 13px; font-weight: 600; color: #BE123C; margin-bottom: 4px;
}
.gen-failed-reason {
  font-size: 11px; color: #6B7280; margin-bottom: 2px;
  word-break: break-word;
}

/* ── PPT 生成完成卡片 ── */
.gen-done-card {
  min-width: 220px; max-width: 280px;
  background: #F0FDF4; border: 1px solid #BBF7D0;
  border-radius: 10px; padding: 12px 14px;
}
.gen-done-hd {
  display: flex; align-items: center; gap: 6px;
  font-size: 13px; font-weight: 600; color: #15803D; margin-bottom: 4px;
}
.gen-done-title {
  font-size: 12px; color: #374151; margin-bottom: 10px;
  white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.gen-done-actions { display: flex; gap: 6px; flex-wrap: wrap; }

/* 旋转动画（进度图标）*/
.spin-icon { animation: spin 1.2s linear infinite; }
@keyframes spin { to { transform: rotate(360deg); } }

/* ghost 按钮 */
.card-btn-ghost {
  background: transparent; color: #9CA3AF;
  border: 1px solid #E5E7EB;
}
.card-btn-ghost:hover { background: #F3F4F6; color: #374151; }

.tool-card-item-desc {
  font-size: 11px;
  color: #6B7280;
  line-height: 1.5;
  margin: 2px 0 4px;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.tool-card-item-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 4px;
}

.tool-card-item-row .tool-card-item-title { margin-bottom: 0; flex: 1; }

.tool-card-reason { color: #D4AF37; font-style: italic; }

.tool-card-empty {
  color: #A3A3A3;
  font-size: 13px;
  text-align: center;
  padding: 20px 0;
}

/* ── PPT 检索结果卡片 ──────────────────────────────── */
.ppt-result-item {
  display: flex;
  gap: 9px;
  padding: 7px 0;
  border-bottom: 1px solid rgba(14, 165, 233, 0.08);
  align-items: flex-start;
}
.ppt-result-item:last-child { border-bottom: none; padding-bottom: 0; }

.ppt-thumb {
  flex-shrink: 0;
  width: 46px;
  height: 34px;
  border-radius: 5px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1px;
}
.ppt-thumb svg { opacity: 0.8; }
.ppt-thumb-pages {
  font-size: 9px;
  font-weight: 700;
  letter-spacing: 0.01em;
  line-height: 1;
}
.ppt-thumb-blue   { background: linear-gradient(135deg,#DBEAFE,#BFDBFE); color: #1D4ED8; }
.ppt-thumb-indigo { background: linear-gradient(135deg,#E0E7FF,#C7D2FE); color: #4338CA; }
.ppt-thumb-violet { background: linear-gradient(135deg,#EDE9FE,#DDD6FE); color: #7C3AED; }
.ppt-thumb-green  { background: linear-gradient(135deg,#DCFCE7,#BBF7D0); color: #15803D; }
.ppt-thumb-teal   { background: linear-gradient(135deg,#CCFBF1,#99F6E4); color: #0F766E; }
.ppt-thumb-gray   { background: linear-gradient(135deg,#F1F5F9,#E2E8F0); color: #475569; }
.ppt-thumb-dark   { background: linear-gradient(135deg,#1E293B,#334155); color: #CBD5E1; }

.ppt-info { flex: 1; min-width: 0; }

.ppt-info-title {
  font-size: 12px;
  font-weight: 600;
  color: #1E293B;
  line-height: 1.35;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  margin-bottom: 1px;
}
.ppt-info-topic {
  font-size: 11px;
  color: #64748B;
  line-height: 1.3;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  margin-bottom: 2px;
}
.ppt-info-meta {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 10px;
  color: #94A3B8;
  margin-bottom: 3px;
  flex-wrap: wrap;
}
.ppt-meta-tag {
  background: rgba(14, 165, 233, 0.1);
  color: #0284C7;
  padding: 0 4px;
  border-radius: 3px;
  font-size: 10px;
  line-height: 1.5;
}
.ppt-meta-dot { color: #CBD5E1; }
.ppt-meta-score { margin-left: auto; color: #7C3AED; font-weight: 500; }

.ppt-info-reason {
  display: flex;
  align-items: flex-start;
  gap: 3px;
  font-size: 10px;
  color: #7C3AED;
  font-style: italic;
  margin-bottom: 4px;
  line-height: 1.4;
}
.ppt-info-reason svg { flex-shrink: 0; margin-top: 2px; }

.ppt-info-actions { display: flex; gap: 5px; align-items: center; }
.ppt-no-file { font-size: 10px; color: #94A3B8; font-style: italic; }

/* ── 批量下载卡片 ──────────────────────────────────── */
.batch-dl-item { padding: 8px 0; }
.batch-dl-item:last-child { border-bottom: none; }

/* ── 批量删除结果卡片 ──────────────────────────────── */
.batch-result-success { border-color: #BBF7D0; background: #F0FDF4; }
.batch-result-partial { border-color: #FED7AA; background: #FFFBEB; }

.batch-result-stats {
  display: flex;
  gap: 12px;
  margin-bottom: 10px;
  font-size: 13px;
  font-weight: 600;
}
.batch-stat-ok  { color: #059669; }
.batch-stat-fail { color: #DC2626; }

.batch-result-section { margin-top: 8px; }
.batch-result-section-title {
  font-size: 11px;
  font-weight: 600;
  color: #6B7280;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 4px;
}
.batch-result-row {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  padding: 3px 0;
}
.batch-result-row-ok   { color: #059669; }
.batch-result-row-fail { color: #DC2626; }

/* ── 批量删除确认弹窗 PPT 列表 ─────────────────────── */
.batch-confirm-list {
  border: 1px solid #FECACA;
  border-radius: 8px;
  background: #FFF5F5;
  padding: 12px;
}
.batch-confirm-list-hd {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  font-weight: 600;
  color: #DC2626;
  margin-bottom: 10px;
}
.batch-confirm-items {
  display: flex;
  flex-direction: column;
  gap: 4px;
  max-height: 200px;
  overflow-y: auto;
}
.batch-confirm-items::-webkit-scrollbar { width: 4px; }
.batch-confirm-items::-webkit-scrollbar-thumb { background: #FECACA; border-radius: 4px; }
.batch-confirm-item {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  font-size: 13px;
  padding: 6px 8px;
  border-radius: 6px;
  background: rgba(220, 38, 38, 0.04);
  border: 1px solid rgba(220, 38, 38, 0.08);
}
.batch-confirm-index {
  flex-shrink: 0;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: #FEE2E2;
  color: #DC2626;
  font-size: 10px;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-top: 1px;
}
.batch-confirm-item-body {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.batch-confirm-title {
  color: #1F2937;
  font-weight: 600;
  font-size: 13px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  line-height: 1.4;
}
.batch-confirm-meta {
  display: flex;
  align-items: center;
  gap: 3px;
  font-size: 11px;
  color: #9CA3AF;
  flex-wrap: wrap;
}
.batch-confirm-dot { color: #D1D5DB; }
.batch-confirm-tpl {
  background: rgba(59, 130, 246, 0.1);
  color: #3B82F6;
  padding: 0 4px;
  border-radius: 3px;
  font-size: 10px;
}
.batch-confirm-no-file {
  background: #FEF3C7;
  color: #D97706;
  padding: 0 4px;
  border-radius: 3px;
  font-size: 10px;
}
.batch-confirm-topic {
  font-size: 11px;
  color: #6B7280;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  font-style: italic;
  line-height: 1.3;
}

/* ── 偏好洞察卡片 ──────────────────────────────────── */
.insights-card { padding-bottom: 6px; }

.insights-section {
  margin-bottom: 14px;
}
.insights-section:last-child { margin-bottom: 0; }

.insights-section-title {
  font-size: 11px;
  font-weight: 700;
  color: #374151;
  letter-spacing: 0.02em;
  margin-bottom: 8px;
}

/* 热门关键词词云 */
.insights-cloud {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px 8px;
  padding: 4px 0;
  line-height: 1.4;
}
.insights-cloud-word {
  display: inline-block;
  cursor: default;
  transition: transform 0.15s ease, opacity 0.15s ease;
  white-space: nowrap;
  letter-spacing: 0.01em;
}
.insights-cloud-word:hover {
  transform: scale(1.12);
  opacity: 1 !important;
}

/* 常用模板横条图 */
.insights-bar-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.insights-bar-row {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
}
.insights-bar-label {
  flex: 0 0 80px;
  max-width: 80px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: #374151;
}
.insights-bar-track {
  flex: 1;
  height: 6px;
  border-radius: 3px;
  background: #F3F4F6;
  overflow: hidden;
}
.insights-bar-fill {
  height: 100%;
  border-radius: 3px;
  background: linear-gradient(90deg, #6366F1 0%, #818CF8 100%);
  transition: width 0.4s ease;
}
.insights-bar-cnt {
  flex: 0 0 24px;
  text-align: right;
  color: #6B7280;
  font-size: 11px;
}

/* 页数分布格子 */
.insights-pages {
  display: flex;
  gap: 6px;
}
.insights-page-cell {
  flex: 1;
  text-align: center;
  padding: 6px 4px;
  border-radius: 6px;
  background: #F8FAFC;
  border: 1px solid #E5E7EB;
}
.insights-page-val {
  font-size: 15px;
  font-weight: 700;
  color: #1F2937;
  line-height: 1.2;
}
.insights-page-label {
  font-size: 10px;
  color: #9CA3AF;
  margin-top: 2px;
}

/* 用户留存漏斗 */
.insights-funnel {
  display: flex;
  align-items: center;
  gap: 6px;
}
.insights-funnel-step {
  flex: 1;
  text-align: center;
  padding: 6px;
  border-radius: 8px;
  background: linear-gradient(135deg, #F0FDF4 0%, #DCFCE7 100%);
  border: 1px solid #BBF7D0;
}
.insights-funnel-num {
  display: block;
  font-size: 16px;
  font-weight: 700;
  color: #059669;
  line-height: 1.2;
}
.insights-funnel-label {
  display: block;
  font-size: 10px;
  color: #065F46;
  margin-top: 2px;
}

/* ── 高危确认码输入框 ──────────────────────────────── */
.confirm-desc--danger {
  color: #B91C1C;
  font-weight: 500;
}
.confirm-code-wrap {
  margin-top: 16px;
  padding: 14px;
  background: #FFF1F2;
  border: 1px solid #FEE2E2;
  border-radius: 8px;
}
.confirm-code-label {
  display: block;
  font-size: 12px;
  color: #7F1D1D;
  margin-bottom: 8px;
  font-weight: 500;
}
.confirm-code-keyword {
  background: #FEE2E2;
  color: #991B1B;
  padding: 1px 5px;
  border-radius: 4px;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 12px;
  letter-spacing: 0.05em;
  font-weight: 700;
}
.confirm-code-input {
  width: 100%;
  box-sizing: border-box;
  padding: 8px 12px;
  border: 1.5px solid #FCA5A5;
  border-radius: 6px;
  font-size: 14px;
  font-family: 'SF Mono', 'Fira Code', monospace;
  letter-spacing: 0.08em;
  color: #DC2626;
  background: #FFFFFF;
  outline: none;
  transition: border-color 0.2s;
}
.confirm-code-input:focus { border-color: #DC2626; box-shadow: 0 0 0 3px rgba(220, 38, 38, 0.1); }
.confirm-code-input--error { border-color: #DC2626; }
.confirm-code-error {
  margin-top: 6px;
  font-size: 11px;
  color: #DC2626;
  font-weight: 500;
}

/* ── 维护模式结果卡片 ──────────────────────────────── */
.maintenance-result-card {
  border-radius: 8px;
  padding: 12px;
  border: 1px solid #E5E7EB;
  background: #FAFAFA;
}
.maintenance-result-card--on {
  background: #FFF7ED;
  border-color: #FED7AA;
}
.maintenance-result-card--off {
  background: #F0FDF4;
  border-color: #BBF7D0;
}
.maintenance-status-badge {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px;
  border-radius: 20px;
  font-size: 12px;
  font-weight: 600;
  margin: 8px 0 6px;
}
.badge--on {
  background: #FEF3C7;
  color: #92400E;
  border: 1px solid #FDE68A;
}
.badge--off {
  background: #DCFCE7;
  color: #14532D;
  border: 1px solid #BBF7D0;
}
.maintenance-status-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}
.badge--on .maintenance-status-dot { background: #D97706; }
.badge--off .maintenance-status-dot { background: #16A34A; }
.maintenance-reason {
  font-size: 11.5px;
  color: #6B7280;
  margin-bottom: 6px;
}
.maintenance-tip {
  font-size: 11.5px;
  color: #9CA3AF;
  line-height: 1.5;
}

/* ── 公告列表卡片（P3.4）─────────────────────────────── */
.ann-list-card { gap: 0; }

.ann-item {
  padding: 10px 0;
  border-bottom: 1px solid #F5F5F5;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.ann-item:last-child { border-bottom: none; padding-bottom: 0; }

.ann-item-hd {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}

.ann-pinned-tag {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  font-size: 10px;
  font-weight: 600;
  color: #D97706;
  background: #FEF3C7;
  border: 1px solid #FDE68A;
  padding: 1px 5px;
  border-radius: 4px;
  flex-shrink: 0;
}

.ann-title {
  font-size: 13px;
  font-weight: 600;
  color: #171717;
  line-height: 1.4;
}

.ann-content {
  font-size: 12.5px;
  color: #525252;
  line-height: 1.55;
  white-space: pre-wrap;
  word-break: break-word;
  /* 超过 3 行省略 */
  display: -webkit-box;
  -webkit-line-clamp: 3;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.ann-meta {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: #A3A3A3;
  flex-wrap: wrap;
}

.ann-time { color: #A3A3A3; }

.ann-expires { color: #A3A3A3; }
.ann-expires--never { color: #10B981; font-weight: 500; }

/* 公告发布成功卡片 */
.ann-created-card {
  border-left: 3px solid #10B981;
}

.ann-created-title {
  font-size: 13px;
  font-weight: 600;
  color: #171717;
  margin: 4px 0 2px;
}

.ann-created-content {
  font-size: 12.5px;
  color: #525252;
  line-height: 1.55;
  white-space: pre-wrap;
  word-break: break-word;
  margin-bottom: 6px;
}

.ann-created-meta {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: #A3A3A3;
}

/* 无障碍：减少动画 */
@media (prefers-reduced-motion: reduce) {
  * { animation: none !important; transition-duration: 0.01ms !important; }
}
</style>





<style>
/* 确认弹窗全局样式 - 极简仪表盘风 */
.tutu-confirm-dialog { z-index: 9100 !important; font-family: 'Inter', sans-serif; }
.tutu-confirm-dialog .el-dialog {
  border-radius: 12px;
  overflow: hidden;
  box-shadow: 0 20px 40px -10px rgba(0,0,0,0.15), 0 0 1px rgba(0,0,0,0.1);
  border: 1px solid #E5E5E5;
}
.tutu-confirm-dialog .el-dialog__header {
  background: #FFFFFF;
  padding: 20px 24px;
  margin: 0;
  border-bottom: 1px solid #F5F5F5;
}
.tutu-confirm-dialog .el-dialog__title {
  color: #171717;
  font-weight: 600;
  font-size: 16px;
}
.tutu-confirm-dialog .el-dialog__headerbtn { top: 20px; right: 20px; }
.tutu-confirm-dialog .el-dialog__headerbtn .el-dialog__close { color: #A3A3A3; font-size: 18px; }
.tutu-confirm-dialog .el-dialog__headerbtn:hover .el-dialog__close { color: #171717; }
.tutu-confirm-dialog .el-dialog__body { padding: 24px; background: #FFFFFF; }
.tutu-confirm-dialog .el-dialog__footer {
  padding: 16px 24px;
  background: #FAFAFA;
  border-top: 1px solid #E5E5E5;
}
.tutu-confirm-dialog .el-button {
  border-radius: 6px;
  padding: 8px 16px;
  font-weight: 500;
  border: 1px solid #E5E5E5;
  color: #404040;
  background: #FFFFFF;
  transition: all 0.2s;
}
.tutu-confirm-dialog .el-button:hover { background: #FAFAFA; color: #171717; border-color: #D4D4D4; }

.tutu-confirm-dialog .el-button--primary {
  background: #171717;
  border-color: #171717;
  color: #FFFFFF;
}
.tutu-confirm-dialog .el-button--primary:hover {
  background: #404040;
  border-color: #404040;
  color: #FFFFFF;
}

/* 高危确认弹窗：红色头部边框 */
.tutu-confirm-dialog--danger .el-dialog {
  border-color: #FCA5A5;
}
.tutu-confirm-dialog--danger .el-dialog__header {
  background: #FFF1F2;
  border-bottom-color: #FEE2E2;
}
.tutu-confirm-dialog--danger .el-dialog__title { color: #B91C1C; }

/* 高危确认弹窗：危险按钮 */
.tutu-confirm-dialog .el-button--danger {
  background: #DC2626;
  border-color: #DC2626;
  color: #FFFFFF;
}
.tutu-confirm-dialog .el-button--danger:hover {
  background: #B91C1C;
  border-color: #B91C1C;
  color: #FFFFFF;
}
.tutu-confirm-dialog .el-button--danger.is-disabled {
  background: #FCA5A5;
  border-color: #FCA5A5;
  cursor: not-allowed;
}
</style>
