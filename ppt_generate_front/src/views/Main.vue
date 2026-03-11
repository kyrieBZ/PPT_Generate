<template>
  <div class="main-container">
    <aside class="sidebar">
      <div class="sidebar-header">
        <h2>PPT智能生成系统</h2>
        <div class="user-info">
          <div class="avatar">{{ userInitials }}</div>
          <div class="user-details">
            <p class="username">{{ currentUser?.username }}</p>
            <p class="user-email">{{ currentUser?.email }}</p>
          </div>
        </div>
      </div>

      <nav class="sidebar-nav">
        <router-link
          v-for="item in menuItems"
          :key="item.id"
          :to="item.path"
          class="nav-item"
          :class="{ 'active': activeMenu === item.id }"
          @click="activeMenu = item.id"
        >
          <el-icon class="nav-icon"><component :is="item.icon" /></el-icon>
          <span class="nav-text">{{ item.text }}</span>
        </router-link>
      </nav>

      <div class="sidebar-footer">
        <button
          v-if="currentUser?.isAdmin"
          class="admin-entry"
          @click="router.push('/admin')"
        >
          <el-icon class="admin-entry-icon"><Setting /></el-icon>
          <span>后台管理</span>
        </button>
        <button @click="handleLogout" class="logout-btn">
          <el-icon class="logout-icon"><SwitchButton /></el-icon>
          <span>退出登录</span>
        </button>
      </div>
    </aside>

    <main class="main-content">
      <header class="main-header">
        <div class="header-left">
          <h1>{{ activeMenuItem?.text || 'PPT智能生成系统' }}</h1>
          <p class="page-description">{{ activeMenuItem?.description || '基于GenAI的智能PPT生成平台' }}</p>
        </div>
        <div class="header-right">
          
          <button class="generate-btn" @click="openGeneratePanel">
            <el-icon class="btn-icon"><Plus /></el-icon>
            <span>新建PPT</span>
          </button>
        </div>
      </header>

      <div class="content-area">
        <section v-if="activeMenu === 'dashboard'" class="dashboard">
          <div class="stats-grid">
            <div v-for="card in statCards" :key="card.id" class="stat-card">
              <el-icon class="stat-icon"><component :is="card.icon" /></el-icon>
              <div class="stat-content">
                <h3>{{ card.title }}</h3>
                <p class="stat-number">{{ card.value }}</p>
              </div>
            </div>
          </div>

          <div class="features-grid">
            <div class="feature-card">
              <el-icon class="feature-icon"><MagicStick /></el-icon>
              <h4>智能生成</h4>
              <p>基于GenAI模型自动生成PPT内容</p>
              <button class="feature-action" @click="openGeneratePanel">
                <span class="btn-content">
                  <el-icon class="btn-icon"><MagicStick /></el-icon>
                  <span>开始生成</span>
                </span>
              </button>
            </div>
            <div class="feature-card">
              <el-icon class="feature-icon"><Brush /></el-icon>
              <h4>个性化定制</h4>
              <p>多通道指令满足个性化需求</p>
              <button class="feature-action">查看模板</button>
            </div>
            <div class="feature-card">
              <el-icon class="feature-icon"><DataAnalysis /></el-icon>
              <h4>图文表生成</h4>
              <p>智能生成文本、图片和表格</p>
              <button class="feature-action">使用示例</button>
            </div>
          </div>
        </section>

        <section v-if="activeMenu === 'generate'" class="generate-section">
          <div class="generate-hero">
            <div>
              <h2>设计驱动的PPT生成</h2>
              <p>先规划大纲，再填充内容，让每一页更有逻辑和节奏。</p>
            </div>
            <div class="generate-hero-tags">
              <span>Outline-first</span>
              <span>Design-led</span>
              <span>Editable</span>
            </div>
          </div>

          <div class="generate-layout">
            <div class="generate-pagination">
              <button
                v-for="panel in generatePanels"
                :key="panel.id"
                class="pagination-tab"
                :class="{ active: activeGeneratePanel === panel.id }"
                @click="setGeneratePanel(panel.id)"
              >
                <span class="tab-index">{{ panel.index }}</span>
                <span>{{ panel.label }}</span>
              </button>
            </div>

            <div v-if="activeGeneratePanel === 'settings'" class="generate-workbench">

              <!-- 来源选择卡片 -->
              <div class="step-card source-card">
                <div class="step-header">
                  <span class="step-index">00</span>
                  <div>
                    <h3>内容来源</h3>
                    <p>选择手动输入主题，或从已上传的文档中提取内容。</p>
                  </div>
                </div>
                <div class="step-body">
                  <div class="source-tabs">
                    <button
                      class="source-tab"
                      :class="{ active: generateSource === 'manual' }"
                      @click="generateSource = 'manual'; generateForm.materialId = ''"
                    >手动输入主题</button>
                    <button
                      class="source-tab"
                      :class="{ active: generateSource === 'material' }"
                      @click="generateSource = 'material'"
                    >从文档提取</button>
                  </div>

                  <!-- 文档上传区 -->
                  <div v-if="generateSource === 'material'" class="material-upload-area">
                    <div class="upload-zone" @click="$refs.materialFileInput.click()">
                      <el-icon style="font-size:2rem;color:#0EA5E9"><Document /></el-icon>
                      <p v-if="!materialUploadFile">点击或拖拽上传文件（PDF / DOCX / TXT，最大 20MB）</p>
                      <p v-else class="upload-filename">{{ materialUploadFile.name }}</p>
                    </div>
                    <input
                      ref="materialFileInput"
                      type="file"
                      accept=".pdf,.docx,.txt"
                      style="display:none"
                      @change="handleMaterialFileChange"
                    />

                    <div v-if="materialUploadFile && !materialCurrentId" class="upload-actions">
                      <button
                        class="action-primary"
                        :disabled="materialUploading"
                        @click="handleMaterialUpload"
                      >
                        {{ materialUploading ? `上传中 ${materialUploadProgress}%` : '开始上传并提取' }}
                      </button>
                    </div>

                    <!-- 提取进度 -->
                    <div v-if="materialCurrentId" class="extract-status">
                      <el-tag :type="materialStatusType(materialStatus)" size="large">
                        {{ materialStatusLabel(materialStatus) }}
                      </el-tag>
                      <div v-if="materialStatus === 'extracting' || materialStatus === 'pending'" class="extract-spinner">
                        <span class="spinner-dot"></span> AI 正在分析文档内容...
                      </div>
                    </div>

                    <!-- 提取结果预览 -->
                    <div v-if="materialExtractResult" class="extract-result-card">
                      <h4>提取结果预览</h4>
                      <div class="extract-field" v-if="materialExtractResult.title">
                        <span class="field-label">标题</span>
                        <span>{{ materialExtractResult.title }}</span>
                      </div>
                      <div class="extract-field" v-if="materialExtractResult.summary">
                        <span class="field-label">摘要</span>
                        <span>{{ materialExtractResult.summary }}</span>
                      </div>
                      <div class="extract-field" v-if="materialExtractResult.outline?.length">
                        <span class="field-label">大纲</span>
                        <ul class="extract-list">
                          <li v-for="(item, i) in materialExtractResult.outline" :key="i">{{ item }}</li>
                        </ul>
                      </div>
                      <div class="extract-field" v-if="materialExtractResult.keywords?.length">
                        <span class="field-label">关键词</span>
                        <div class="keyword-tags">
                          <el-tag v-for="kw in materialExtractResult.keywords" :key="kw" size="small" type="info">{{ kw }}</el-tag>
                        </div>
                      </div>
                      <div class="extract-actions">
                        <button class="action-primary" @click="useMaterialForGenerate">
                          使用此内容填充生成参数
                        </button>
                      </div>
                    </div>

                    <!-- 也可从历史材料选择 -->
                    <div v-if="!materialCurrentId" class="material-history-hint">
                      <span>或从</span>
                      <button class="link-btn" @click="activeMenu = 'materials'; router.push('/main/materials')">我的材料</button>
                      <span>中选择已提取的文档</span>
                    </div>
                  </div>

                  <!-- 已绑定材料提示 -->
                  <div v-if="generateSource === 'manual' && generateForm.materialId" class="material-bound-hint">
                    <el-tag type="success" size="small">已关联文档材料</el-tag>
                    <button class="link-btn" @click="generateForm.materialId = ''">取消关联</button>
                  </div>
                </div>
              </div>

              <div class="step-card">
                <div class="step-header">
                  <span class="step-index">01</span>
                  <div>
                    <h3>基础信息</h3>
                    <p>定义主题和结构骨架，为大纲生成提供方向。</p>
                  </div>
                </div>
                <div class="step-body">
                  <div class="form-group">
                    <label for="ppt-title">PPT标题</label>
                    <input
                      id="ppt-title"
                      v-model="generateForm.title"
                      type="text"
                      placeholder="请输入PPT标题"
                    />
                  </div>

                  <div class="form-group">
                    <label for="ppt-topic">主题/内容描述</label>
                    <textarea
                      id="ppt-topic"
                      v-model="generateForm.topic"
                      placeholder="请详细描述PPT的主题和内容要求..."
                      rows="4"
                    ></textarea>
                  </div>

                  <div class="form-group">
                    <label for="ppt-pages">期望页数</label>
                    <input
                      id="ppt-pages"
                      v-model.number="generateForm.pages"
                      type="number"
                      min="1"
                      max="50"
                    />
                  </div>
                </div>
              </div>

              <div class="step-card">
                <div class="step-header">
                  <span class="step-index">02</span>
                  <div>
                    <h3>生成方式</h3>
                  </div>
                </div>
                <div class="step-body">
                  <div class="form-group">
                    <el-radio-group v-model="generateForm.generateMode" class="generate-mode-radio">
                      <el-radio-button value="template">基于模板</el-radio-button>
                      <el-radio-button value="style">基于预设主题</el-radio-button>
                    </el-radio-group>
                  </div>
                  <div v-if="generateForm.generateMode === 'template'" class="form-group">
                    <label>选择模板</label>
                    <div class="template-options" v-if="!templatesLoading && templates.length">
                      <label
                        v-for="tpl in templates"
                        :key="tpl.id"
                        class="template-option"
                        :class="{ selected: generateForm.templateId === tpl.id }"
                      >
                        <input
                          type="radio"
                          :value="tpl.id"
                          v-model="generateForm.templateId"
                          hidden
                        />
                        <div class="template-thumb">
                          <img
                            v-if="tpl.previewImage || tpl.preview_image"
                            :src="tpl.previewImage || tpl.preview_image"
                            :alt="tpl.name"
                            loading="lazy"
                          >
                          <div v-else class="template-thumb-fallback">无预览</div>
                        </div>
                        <div class="template-option-info">
                          <div class="template-title-row">
                            <strong>{{ tpl.name }}</strong>
                            <span class="template-badge" v-if="tpl.localDownloadUrl">内置模板</span>
                          </div>
                          <small>{{ tpl.provider }}</small>
                          <p>{{ tpl.description }}</p>
                          <div class="template-option-tags" v-if="tpl.tags?.length">
                            <span v-for="tag in tpl.tags" :key="tag">#{{ tag }}</span>
                          </div>
                        </div>
                      </label>
                    </div>
                    <div v-else class="templates-empty">模板列表加载中...</div>
                  </div>
                  <div v-if="generateForm.generateMode === 'style'" class="form-group">
                    <label>选择预设主题</label>
                    <div class="style-options">
                      <label
                        v-for="style in styles"
                        :key="style.id"
                        class="style-option"
                        :class="{ 'selected': generateForm.style === style.id }"
                      >
                        <input
                          type="radio"
                          v-model="generateForm.style"
                          :value="style.id"
                          hidden
                        />
                        {{ style.name }}
                      </label>
                    </div>
                  </div>
                </div>
              </div>

              <div class="step-card">
                <div class="step-header">
                  <span class="step-index">03</span>
                  <div>
                    <h3>模型与生成选项</h3>
                    <p>确定生成策略与展示能力。</p>
                  </div>
                </div>
                <div class="step-body">
                  <div class="form-group">
                    <label>生成选项</label>
                    <div class="generate-options">
                      <label class="option-checkbox">
                        <input type="checkbox" v-model="generateForm.includeImages" />
                        包含图片
                      </label>
                      <label class="option-checkbox">
                        <input type="checkbox" v-model="generateForm.includeCharts" />
                        包含图表
                      </label>
                      <label class="option-checkbox">
                        <input type="checkbox" v-model="generateForm.includeNotes" />
                        包含演讲备注
                      </label>
                      <label class="option-checkbox">
                        <input type="checkbox" v-model="generateForm.enableSectionSlides" />
                        启用章节页
                      </label>
                    </div>
                    <div v-if="generateForm.enableSectionSlides" class="form-group-inline mt-1">
                      <label for="section-interval">每</label>
                      <select id="section-interval" v-model.number="generateForm.sectionSlideInterval" class="select-sm">
                        <option :value="3">3</option>
                        <option :value="4">4</option>
                        <option :value="5">5</option>
                        <option :value="6">6</option>
                      </select>
                      <span>页插入一章节页</span>
                    </div>
                  </div>
                  <div v-if="generateForm.generateMode === 'style'" class="form-group">
                    <label for="theme-preset">主题预设（PptxGenJS）</label>
                    <select id="theme-preset" v-model="generateForm.themePreset" class="select-md">
                      <option value="">默认（随上方预设主题）</option>
                      <option value="midnight">深蓝商务 (Midnight)</option>
                      <option value="forest">森林绿 (Forest)</option>
                      <option value="charcoal">炭灰简约 (Charcoal)</option>
                      <option value="coral">珊瑚活力 (Coral)</option>
                      <option value="teal">青绿信任 (Teal)</option>
                      <option value="ocean">海洋渐变 (Ocean)</option>
                      <option value="berry">浆果奶油 (Berry)</option>
                      <option value="sage">鼠尾草 (Sage)</option>
                      <option value="terracotta">暖陶 (Terracotta)</option>
                      <option value="cherry">樱桃 (Cherry)</option>
                    </select>
                  </div>

                  <div class="form-group">
                    <label>选择模型</label>
                    <div class="model-options" v-if="!modelsLoading && models.length">
                      <label
                        v-for="model in models"
                        :key="model.id"
                        class="model-option"
                        :class="{ selected: generateForm.modelId === model.id }"
                      >
                        <input
                          type="radio"
                          :value="model.id"
                          v-model="generateForm.modelId"
                          hidden
                        />
                        <div class="model-title">{{ model.name }}</div>
                        <p class="model-desc">{{ model.description }}</p>
                        <small>{{ model.provider }} · {{ model.locale }}</small>
                      </label>
                    </div>
                    <div v-else class="templates-empty">模型列表加载中...</div>
                  </div>
                </div>
              </div>

              <div class="action-bar">
                <button class="action-secondary" @click="resetGenerateForm">重置</button>
              </div>
            </div>

            <div v-if="activeGeneratePanel === 'outline'" class="outline-panel">
              <div class="outline-panel-header">
                <div class="outline-panel-title">
                  <span class="step-index">OUTLINE</span>
                  <div>
                    <h3>大纲设计</h3>
                    <p>共 <strong>{{ outlineItems.length }}</strong> 页 · 可拖拽调整顺序，点击卡片展开编辑</p>
                  </div>
                </div>
                <div class="outline-toolbar">
                  <button
                    class="outline-btn primary"
                    @click="handleGenerateOutline"
                    :disabled="outlineLoading"
                  >
                    <span v-if="outlineLoading" class="outline-spinner"></span>
                    <span v-else>{{ outlineReady ? '重新生成' : 'AI 生成大纲' }}</span>
                  </button>
                  <button
                    v-if="outlineReady"
                    class="outline-btn"
                    @click="addOutlineItem"
                    title="在末尾新增一页"
                  >
                    + 新增页
                  </button>
                </div>
              </div>

              <!-- 加载骨架屏 -->
              <div v-if="outlineLoading" class="outline-skeleton">
                <div v-for="n in 4" :key="n" class="outline-skeleton-item">
                  <div class="skeleton-line short"></div>
                  <div class="skeleton-line"></div>
                  <div class="skeleton-line medium"></div>
                </div>
              </div>

              <!-- 大纲列表 -->
              <div v-else-if="outlineReady" class="outline-list">
                <div
                  v-for="(item, index) in outlineItems"
                  :key="`outline-${index}`"
                  class="outline-item"
                  :class="[`outline-type-${item.pageType || 'content'}`, { 'is-expanded': outlineExpandedIndex === index }]"
                >
                  <!-- 卡片头部：点击折叠/展开 -->
                  <div class="outline-item-header" @click="toggleOutlineExpand(index)">
                    <div class="outline-item-meta">
                      <span class="outline-page-badge">P{{ index + 1 }}</span>
                      <span class="outline-type-tag" :class="`tag-${item.pageType || 'content'}`">
                        {{ outlinePageTypeLabel(item.pageType) }}
                      </span>
                      <span class="outline-item-title-preview">{{ item.title || '（未填写标题）' }}</span>
                    </div>
                    <div class="outline-item-actions">
                      <button
                        class="outline-icon-btn"
                        title="上移"
                        :disabled="index === 0"
                        @click.stop="moveOutlineItem(index, -1)"
                      >↑</button>
                      <button
                        class="outline-icon-btn"
                        title="下移"
                        :disabled="index === outlineItems.length - 1"
                        @click.stop="moveOutlineItem(index, 1)"
                      >↓</button>
                      <button
                        class="outline-icon-btn danger"
                        title="删除此页"
                        @click.stop="removeOutlineItem(index)"
                      >✕</button>
                      <span class="outline-expand-arrow">{{ outlineExpandedIndex === index ? '▲' : '▼' }}</span>
                    </div>
                  </div>

                  <!-- 展开编辑区 -->
                  <div v-show="outlineExpandedIndex === index" class="outline-item-body">
                    <!-- 页面类型选择 -->
                    <div class="outline-field">
                      <label class="outline-field-label">页面类型</label>
                      <div class="outline-type-selector">
                        <button
                          v-for="pt in outlinePageTypes"
                          :key="pt.value"
                          class="type-option"
                          :class="{ active: item.pageType === pt.value }"
                          @click="item.pageType = pt.value"
                        >{{ pt.label }}</button>
                      </div>
                    </div>

                    <!-- 标题 -->
                    <div class="outline-field">
                      <label class="outline-field-label">
                        页面标题
                        <span class="outline-char-count" :class="{ warn: item.title.length > 16 }">
                          {{ item.title.length }}/18
                        </span>
                      </label>
                      <input
                        v-model="item.title"
                        type="text"
                        class="outline-input"
                        placeholder="请输入页面标题（≤18字）"
                        maxlength="18"
                      />
                    </div>

                    <!-- 要点列表 -->
                    <div class="outline-field">
                      <label class="outline-field-label">
                        内容要点
                        <span class="outline-hint">每条独立编辑，按 Enter 可快速新增</span>
                      </label>
                      <div class="outline-keypoints">
                        <div
                          v-for="(kp, kpIndex) in item.keyPoints"
                          :key="`kp-${index}-${kpIndex}`"
                          class="outline-keypoint-row"
                        >
                          <span class="kp-dot">·</span>
                          <input
                            v-model="item.keyPoints[kpIndex]"
                            type="text"
                            class="outline-input kp-input"
                            :placeholder="`要点 ${kpIndex + 1}（≤25字）`"
                            maxlength="25"
                            @keydown.enter.prevent="addKeyPoint(index, kpIndex)"
                            @keydown.backspace="onKeyPointBackspace(index, kpIndex, $event)"
                          />
                          <button
                            class="kp-remove-btn"
                            title="删除此要点"
                            @click="removeKeyPoint(index, kpIndex)"
                          >✕</button>
                        </div>
                        <button
                          v-if="item.keyPoints.length < 6"
                          class="kp-add-btn"
                          @click="addKeyPoint(index, item.keyPoints.length - 1)"
                        >+ 添加要点</button>
                      </div>
                    </div>

                    <!-- 摘要 -->
                    <div class="outline-field">
                      <label class="outline-field-label">
                        页面摘要
                        <span class="outline-hint">可选，用于辅助 AI 生成内容</span>
                        <span class="outline-char-count" :class="{ warn: item.summary.length > 36 }">
                          {{ item.summary.length }}/40
                        </span>
                      </label>
                      <input
                        v-model="item.summary"
                        type="text"
                        class="outline-input"
                        placeholder="本页核心目的（≤40字，可留空）"
                        maxlength="40"
                      />
                    </div>
                  </div>
                </div>

                <!-- 末尾新增按钮 -->
                <button class="outline-add-card" @click="addOutlineItem">
                  <span>＋ 新增一页</span>
                </button>
              </div>

              <!-- 空状态 -->
              <div v-else class="outline-empty">
                <div class="outline-empty-icon">📋</div>
                <p>点击「AI 生成大纲」，根据您的主题自动规划页面结构</p>
              </div>

              <!-- 底部操作栏 -->
              <div v-if="outlineReady" class="outline-footer">
                <span class="outline-footer-tip">确认大纲后开始生成 PPT</span>
                <button
                  class="action-primary"
                  @click="handleGenerate"
                  :disabled="generating || outlineLoading"
                >
                  {{ generating ? '生成中...' : '开始生成 PPT →' }}
                </button>
              </div>
            </div>

            <div v-if="activeGeneratePanel === 'preview'" class="generate-preview-panel">
              <div class="panel-title">
                <div>
                  <h3>最新生成预览</h3>
                  <p>完成生成任务后将在此展示生成的PPT预览。</p>
                </div>
                <div v-if="selectedTemplate" class="template-hint">
                  <span>当前模板：{{ selectedTemplate.name }} · {{ selectedTemplate.provider }}</span>
                  <a
                    v-if="selectedTemplate.localDownloadUrl"
                    class="template-download-link"
                    :href="selectedTemplate.localDownloadUrl"
                    download
                  >
                    下载内置模板
                  </a>
                </div>
              </div>
              <div
                v-if="previewEmbedUrl || hasLocalPreview"
                class="preview-card"
                :class="{ 'preview-has-bg': Boolean(previewCardStyle.backgroundImage) }"
                :style="previewCardStyle"
              >
                <div class="preview-header">
                  <div class="preview-label">PPT 预览</div>
                  <div class="preview-actions">
                    <div v-if="previewMode === 'local' && hasLocalPreview" class="preview-counter">
                      第 {{ previewIndex + 1 }} / {{ previewSlideCount }} 页
                    </div>
                    <div class="preview-mode">
                      <button
                        class="preview-toggle"
                        :class="{ active: previewMode === 'online' }"
                        :disabled="!canUseOnlinePreview"
                        @click="setPreviewMode('online')"
                      >
                        在线预览
                      </button>
                      <button
                        class="preview-toggle"
                        :class="{ active: previewMode === 'local' }"
                        :disabled="!canUseLocalPreview"
                        @click="setPreviewMode('local')"
                      >
                        本地预览
                      </button>
                    </div>
                    <el-dropdown
                      trigger="click"
                      :disabled="!hasPreviewFile"
                      @command="(cmd) => cmd === 'pptx' ? downloadPPT(previewRequestItem) : downloadPDF(previewRequestItem)"
                    >
                      <el-button type="success" size="default" :disabled="!hasPreviewFile" class="preview-download-btn">
                        <el-icon class="btn-icon"><Download /></el-icon>
                        <span>下载</span>
                        <el-icon class="el-icon--right"><ArrowDown /></el-icon>
                      </el-button>
                      <template #dropdown>
                        <el-dropdown-menu>
                          <el-dropdown-item command="pptx">下载 PPTX</el-dropdown-item>
                          <el-dropdown-item command="pdf">下载 PDF</el-dropdown-item>
                        </el-dropdown-menu>
                      </template>
                    </el-dropdown>
                  </div>
                </div>
                <div v-if="previewMode === 'online'" class="preview-embed">
                  <iframe
                    v-if="previewEmbedUrl"
                    class="preview-iframe"
                    :src="previewEmbedUrl"
                    title="PPT预览"
                    frameborder="0"
                    allowfullscreen
                  ></iframe>
                  <div v-else class="preview-placeholder">在线预览不可用，请切换到本地预览。</div>
                </div>
                <div v-else class="preview-body" :class="previewLayoutClass">
                  <template v-if="hasLocalPreview">
                    <div class="layout-grid">
                      <div class="layout-text">
                        <h3>{{ currentPreviewSlide?.title || '未命名标题' }}</h3>
                        <ul v-if="currentPreviewBullets.length">
                          <li v-for="(item, index) in currentPreviewBullets" :key="`${previewIndex}-bullet-${index}`">
                            {{ item }}
                          </li>
                        </ul>
                        <div v-else-if="currentPreviewRawText" class="preview-raw">
                          {{ currentPreviewRawText }}
                        </div>
                        <div v-else class="preview-placeholder">暂无正文内容</div>
                        <div v-if="currentPreviewNotes" class="preview-raw">备注：{{ currentPreviewNotes }}</div>
                        <div v-if="currentPreviewSuggestions.length" class="image-prompts">
                          <span v-for="(item, index) in currentPreviewSuggestions" :key="`${previewIndex}-suggestion-${index}`">
                            {{ item }}
                          </span>
                        </div>
                      </div>
                      <div v-if="hasPreviewMedia" class="layout-media">
                        <div v-if="currentPreviewImages.length" class="preview-images">
                          <img
                            v-for="(url, index) in currentPreviewImages"
                            :key="`${previewIndex}-img-${index}`"
                            :src="url"
                            alt="预览图片"
                            loading="lazy"
                          >
                        </div>
                        <div v-else-if="previewFallbackImage" class="preview-template-fallback">
                          <img :src="previewFallbackImage" alt="模板预览">
                        </div>
                        <div v-if="currentPreviewPrompts.length" class="image-prompts">
                          <span v-for="(item, index) in currentPreviewPrompts" :key="`${previewIndex}-prompt-${index}`">
                            {{ item }}
                          </span>
                        </div>
                      </div>
                    </div>
                    <div class="preview-controls">
                      <button class="preview-nav" :disabled="previewIndex === 0" @click="goPreviewPrev">上一页</button>
                      <button class="preview-nav" :disabled="previewIndex >= previewSlideCount - 1" @click="goPreviewNext">
                        下一页
                      </button>
                    </div>
                    <div class="preview-thumbnails">
                      <button
                        v-for="(slide, index) in previewSlides"
                        :key="`thumb-${index}`"
                        class="preview-thumb"
                        :class="{ active: index === previewIndex }"
                        @click="selectPreviewSlide(index)"
                      >
                        <span class="thumb-index">{{ index + 1 }}</span>
                        <span>{{ slide.title || '未命名' }}</span>
                      </button>
                    </div>
                  </template>
                  <div v-else class="preview-placeholder">本地预览暂无数据，请先生成PPT。</div>
                </div>
              </div>
              <div v-else class="preview-empty">暂无PPT预览，请先生成PPT。</div>
            </div>

            <div class="generate-nav">
              <button class="action-secondary" :disabled="!canGoPrevPanel" @click="goPrevPanel">上一步</button>
              <button class="action-primary" :disabled="!canGoNextPanel" @click="goNextPanel">下一步</button>
            </div>
          </div>
        </section>

        <section v-if="activeMenu === 'history'" class="history-section">
          <div class="section-header">
            <h2>生成历史</h2>
            <div class="search-box history-search-box">
              <el-input
                v-model="historyQuery"
                placeholder="搜索标题或主题..."
                clearable
                @keyup.enter="triggerHistorySearch"
              >
                <template #prefix>
                  <el-icon><Search /></el-icon>
                </template>
              </el-input>
              <el-button
                class="search-icon-btn"
                circle
                :icon="Search"
                @click="triggerHistorySearch"
              />
            </div>
          </div>

          <div v-if="historyBusy" class="history-empty">{{ historyBusyLabel }}</div>
          <div v-else-if="!filteredHistory.length" class="history-empty">暂无记录，立即生成第一份PPT吧！</div>
          <div v-else class="history-list">
            <div v-for="item in pagedHistory" :key="item.id" class="history-item">
              <div class="history-preview">
                <div class="preview-icon">📄</div>
                <div class="preview-content">
                  <h4>{{ item.title }}</h4>
                  <p>{{ item.topic }}</p>
                  <div class="history-meta">
                    <span class="meta-item">生成时间: {{ formatDate(item.createdAt) }}</span>
                    <span class="meta-item">页数: {{ item.pages }}</span>
                    <span class="meta-item">状态: {{ item.status }}</span>
                    <span class="meta-item">{{ historySourceDisplay(item) }}</span>
                  </div>
                </div>
              </div>
              <div class="history-actions">
                <el-button size="large" type="primary" @click="editPPT(item)">
                  <span class="btn-content">
                    <el-icon class="btn-icon"><EditPen /></el-icon>
                    <span>编辑</span>
                  </span>
                </el-button>
                <el-dropdown
                  trigger="click"
                  :disabled="!item?.hasFile"
                  @command="(cmd) => cmd === 'pptx' ? downloadPPT(item) : downloadPDF(item)"
                >
                  <el-button size="large" type="success" :disabled="!item?.hasFile">
                    <span class="btn-content">
                      <el-icon class="btn-icon"><Download /></el-icon>
                      <span>下载</span>
                      <el-icon class="el-icon--right"><ArrowDown /></el-icon>
                    </span>
                  </el-button>
                  <template #dropdown>
                    <el-dropdown-menu>
                      <el-dropdown-item command="pptx">下载 PPTX</el-dropdown-item>
                      <el-dropdown-item command="pdf">下载 PDF</el-dropdown-item>
                    </el-dropdown-menu>
                  </template>
                </el-dropdown>
                <el-button size="large" type="danger" @click="deleteHistory(item)">
                  <span class="btn-content">
                    <el-icon class="btn-icon"><Delete /></el-icon>
                    <span>删除</span>
                  </span>
                </el-button>
              </div>
            </div>
          </div>
          <div v-if="showHistoryPagination && !historyBusy" class="history-pagination">
            <el-pagination
              background
              layout="total, sizes, prev, pager, next"
              :total="historyTotal"
              :page-size="historyPageSize"
              :page-sizes="[6, 10, 15, 20]"
              :current-page="historyPage"
              @current-change="handleHistoryPageChange"
              @size-change="handleHistorySizeChange"
            />
          </div>
        </section>

        <section v-if="activeMenu === 'templates'" class="templates-section">
          <div class="section-header">
            <h2>模板中心</h2>
            <div class="search-box template-search-box">
              <el-input
                v-model="templateQuery"
                placeholder="搜索模板名称、标签或来源..."
                clearable
                @keyup.enter="triggerTemplateSearch"
              >
                <template #prefix>
                  <el-icon><Search /></el-icon>
                </template>
              </el-input>
              <el-button
                class="search-icon-btn"
                circle
                :icon="Search"
                :loading="templateSearchLoading"
                @click="triggerTemplateSearch"
              />
            </div>
          </div>

          <div v-if="templatesBusy" class="templates-empty">{{ templatesBusyLabel }}</div>
          <div v-else-if="!filteredTemplates.length" class="templates-empty">
            暂无匹配的模板，稍后再试或更换关键词。
          </div>
          <div v-else class="templates-grid">
            <div v-for="template in filteredTemplates" :key="template.id" class="template-card">
              <div class="template-preview">
                <img :src="template.previewImage" :alt="template.name" v-if="template.previewImage" loading="lazy">
                <div class="default-preview" v-else>
                  <div class="preview-slides">
                    <div class="slide" v-for="n in 3" :key="n"></div>
                  </div>
                </div>
              </div>
              <div class="template-info">
                <div class="template-title-row">
                  <h4>{{ template.name }}</h4>
                  <span v-if="template.localDownloadUrl" class="template-badge">内置模板</span>
                </div>
                <p class="template-desc">{{ template.description }}</p>
                <div class="template-meta">
                  <span>来源：
                    <a :href="template.providerUrl" target="_blank" rel="noopener">
                      {{ template.provider }}
                    </a>
                  </span>
                  <span>授权：{{ template.license }}</span>
                </div>
                <div v-if="template.tags?.length" class="template-tags">
                  <span v-for="tag in template.tags" :key="tag" class="template-tag">#{{ tag }}</span>
                </div>
                <div class="template-actions">
                  <button class="use-template-btn" @click="useTemplate(template)">快速填充</button>
                  <a
                    v-if="template.localDownloadUrl"
                    class="use-template-btn outline"
                    :href="template.localDownloadUrl"
                    download
                  >
                    下载内置
                  </a>
                  <a
                    class="use-template-btn secondary"
                    :href="template.downloadUrl"
                    target="_blank"
                    rel="noopener"
                  >
                    打开源站
                  </a>
                </div>
              </div>
            </div>

          </div>
        </section>

        <!-- ===== 我的材料 ===== -->
        <section v-if="activeMenu === 'materials'" class="materials-section">
          <div class="materials-header">
            <div>
              <h2>我的材料</h2>
              <p>上传教学材料或论文文献，AI 自动提取关键信息，一键用于 PPT 生成。</p>
            </div>
            <button class="generate-btn" @click="activeMenu = 'generate'; router.push('/main/generate'); generateSource = 'material'">
              <el-icon class="btn-icon"><Plus /></el-icon>
              <span>上传新材料</span>
            </button>
          </div>

          <div v-if="materialsLoading" class="materials-loading">加载中...</div>

          <div v-else-if="!materialsList.length" class="materials-empty">
            <el-icon style="font-size:3rem;color:#94a3b8"><Document /></el-icon>
            <p>暂无材料，前往智能生成页上传文档</p>
            <button class="action-primary" @click="activeMenu = 'generate'; router.push('/main/generate'); generateSource = 'material'">
              去上传
            </button>
          </div>

          <div v-else class="materials-grid">
            <div v-for="mat in materialsList" :key="mat.id" class="material-card">
              <div class="material-card-header">
                <div class="material-icon">
                  <el-icon style="font-size:1.5rem"><Document /></el-icon>
                </div>
                <div class="material-info">
                  <div class="material-filename">{{ mat.filename }}</div>
                  <div class="material-meta">
                    <span class="material-type">{{ mat.fileType?.toUpperCase() }}</span>
                    <span>{{ (mat.fileSize / 1024).toFixed(1) }} KB</span>
                    <span>{{ formatDate(mat.createdAt) }}</span>
                  </div>
                </div>
              </div>
              <div class="material-card-footer">
                <el-tag :type="materialStatusType(mat.status)" size="small">
                  {{ materialStatusLabel(mat.status) }}
                </el-tag>
                <div class="material-actions">
                  <button
                    v-if="mat.status === 'completed'"
                    class="mat-btn primary"
                    @click="viewMaterialDetail(mat.id)"
                  >查看结果</button>
                  <button
                    class="mat-btn danger"
                    @click="deleteMaterial(mat.id, mat.filename)"
                  >删除</button>
                </div>
              </div>
            </div>
          </div>
        </section>

        <!-- 材料详情对话框 -->
        <div v-if="materialDetailVisible" class="material-modal-overlay" @click.self="materialDetailVisible = false">
          <div class="material-modal">
            <div class="material-modal-header">
              <h3>提取结果详情</h3>
              <button class="modal-close" @click="materialDetailVisible = false">✕</button>
            </div>
            <div class="material-modal-body" v-if="materialDetailData">
              <div class="extract-field" v-if="materialDetailData.extractResult?.title">
                <span class="field-label">标题</span>
                <span>{{ materialDetailData.extractResult.title }}</span>
              </div>
              <div class="extract-field" v-if="materialDetailData.extractResult?.summary">
                <span class="field-label">摘要</span>
                <p>{{ materialDetailData.extractResult.summary }}</p>
              </div>
              <div class="extract-field" v-if="materialDetailData.extractResult?.outline?.length">
                <span class="field-label">大纲</span>
                <ul class="extract-list">
                  <li v-for="(item, i) in materialDetailData.extractResult.outline" :key="i">{{ item }}</li>
                </ul>
              </div>
              <div class="extract-field" v-if="materialDetailData.extractResult?.key_points?.length">
                <span class="field-label">核心论点</span>
                <ul class="extract-list">
                  <li v-for="(kp, i) in materialDetailData.extractResult.key_points" :key="i">{{ kp }}</li>
                </ul>
              </div>
              <div class="extract-field" v-if="materialDetailData.extractResult?.data_mentions?.length">
                <span class="field-label">关键数据</span>
                <ul class="extract-list">
                  <li v-for="(d, i) in materialDetailData.extractResult.data_mentions" :key="i">{{ d }}</li>
                </ul>
              </div>
              <div class="extract-field" v-if="materialDetailData.extractResult?.keywords?.length">
                <span class="field-label">关键词</span>
                <div class="keyword-tags">
                  <el-tag v-for="kw in materialDetailData.extractResult.keywords" :key="kw" size="small" type="info">{{ kw }}</el-tag>
                </div>
              </div>
            </div>
            <div v-else class="material-modal-loading">加载中...</div>
            <div class="material-modal-footer">
              <button class="modal-btn secondary" @click="materialDetailVisible = false">关闭</button>
              <button
                v-if="materialDetailData?.extractResult"
                class="modal-btn primary"
                @click="useMaterialFromList(materialDetailData)"
              >用于生成 PPT</button>
            </div>
          </div>
        </div>

        <section v-if="activeMenu === 'settings'" class="settings-section">
          <div class="settings-card">
            <h2>系统设置</h2>
            <p class="settings-desc">选择默认生成模型，并规划未来自研模型的数据准备。</p>

            <div class="settings-block">
              <h3>默认生成模型</h3>
              <p>根据使用场景选择对应模型，保存后生成页会自动采用该模型。</p>
              <div v-if="modelsLoading" class="templates-empty">模型加载中...</div>
              <div v-else class="settings-models">
                <label
                  v-for="model in models"
                  :key="model.id"
                  class="settings-model"
                  :class="{ selected: selectedModel === model.id }"
                >
                  <input
                    type="radio"
                    :value="model.id"
                    :checked="selectedModel === model.id"
                    @change="handleModelChange(model.id)"
                  />
                  <div class="model-texts">
                    <strong>{{ model.name }}</strong>
                    <small>{{ model.provider }} · {{ model.locale }}</small>
                    <p>{{ model.description }}</p>
                    <div class="model-tags">
                      <span v-if="model.latency">延迟: {{ model.latency }}</span>
                      <span v-if="model.cost">成本: {{ model.cost }}</span>
                    </div>
                  </div>
                </label>
              </div>
            </div>

            <div class="settings-block">
              <h3>自研模型计划</h3>
              <p>记录自建PPT语料与训练想法，便于后续接入 Future PPT Lab 模型。</p>
              <div class="plan-fields">
                <label>
                  数据集名称 / 存储路径
                  <input type="text" v-model="futurePlan.dataset" placeholder="例：/datasets/ppt_corp_v1" />
                </label>
                <label>
                  备注
                  <textarea v-model="futurePlan.notes" rows="4" placeholder="记录采样、清洗、标注等规划..."></textarea>
                </label>
              </div>
              <small>该信息仅保存在浏览器本地，方便随时查看。</small>
            </div>
          </div>
        </section>
      </div>
    </main>

  </div>
</template>

<script setup>
import { ref, reactive, computed, onMounted, onBeforeUnmount, watch , watchEffect } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useStore } from 'vuex'
import { ElMessage, ElMessageBox } from 'element-plus'
import { MagicStick, Download, Delete, EditPen, Search, Setting, SwitchButton, Plus, DataAnalysis, Brush, Document, Timer, Star, RefreshRight, ArrowDown } from '@element-plus/icons-vue'
import templatesAPI from '@/api/templates'
import pptAPI from '@/api/ppt'
import materialAPI from '@/api/material'
import dayjs from 'dayjs'

const route = useRoute()
const router = useRouter()
const store = useStore()

const models = computed(() => store.getters.models || [])
const modelsLoading = computed(() => store.getters.modelsLoading)
const selectedModel = computed(() => store.getters.selectedModel)

const generating = ref(false)
const templateQuery = ref('')
const historyQuery = ref('')
const templateSearchResults = ref([])
const templateSearchLoading = ref(false)
let templateSearchTimer = null
let templateSearchVersion = 0
const historySearchResults = ref([])
const historySearchLoading = ref(false)
let historySearchTimer = null
let historySearchVersion = 0
const historyPage = ref(1)
const historyPageSize = ref(6)
const futurePlan = reactive({
  dataset: localStorage.getItem('futureDataset') || '',
  notes: localStorage.getItem('futureNotes') || ''
})
const previewFileUrl = ref('')
const previewMode = ref('online')
const previewSlides = ref([])
const previewIndex = ref(0)
const previewRequestId = ref(0)
const previewDownloadUrl = ref('')
const previewDownloadUrlPdf = ref('')
const previewTitle = ref('')
const outlineItems = ref([])
const outlineLoading = ref(false)
const outlineExpandedIndex = ref(null)

const outlinePageTypes = [
  { value: 'cover', label: '封面' },
  { value: 'toc', label: '目录' },
  { value: 'content', label: '内容' },
  { value: 'summary', label: '总结' }
]

const outlinePageTypeLabel = (type) => {
  const found = outlinePageTypes.find(t => t.value === type)
  return found ? found.label : '内容'
}

const toggleOutlineExpand = (index) => {
  outlineExpandedIndex.value = outlineExpandedIndex.value === index ? null : index
}

const moveOutlineItem = (index, direction) => {
  const target = index + direction
  if (target < 0 || target >= outlineItems.value.length) return
  const items = outlineItems.value
  const temp = items[index]
  items[index] = items[target]
  items[target] = temp
  outlineExpandedIndex.value = target
}

const addKeyPoint = (itemIndex, afterKpIndex) => {
  const item = outlineItems.value[itemIndex]
  if (!item) return
  item.keyPoints.splice(afterKpIndex + 1, 0, '')
}

const removeKeyPoint = (itemIndex, kpIndex) => {
  const item = outlineItems.value[itemIndex]
  if (!item || item.keyPoints.length <= 1) return
  item.keyPoints.splice(kpIndex, 1)
}

const onKeyPointBackspace = (itemIndex, kpIndex, event) => {
  const item = outlineItems.value[itemIndex]
  if (!item) return
  if (item.keyPoints[kpIndex] === '' && item.keyPoints.length > 1) {
    event.preventDefault()
    removeKeyPoint(itemIndex, kpIndex)
  }
}

// ---- 文档材料相关状态 ----
const generateSource = ref('manual')  // 'manual' | 'material'
const materialUploadFile = ref(null)
const materialUploadProgress = ref(0)
const materialUploading = ref(false)
const materialCurrentId = ref('')
const materialStatus = ref('')  // pending / extracting / completed / failed
const materialExtractResult = ref(null)
const materialPollingTimer = ref(null)
const materialsList = ref([])
const materialsLoading = ref(false)
const materialDetailId = ref('')
const materialDetailData = ref(null)
const materialDetailVisible = ref(false)

const generatePanels = [
  { id: 'settings', label: '生成设置', index: '01' },
  { id: 'outline', label: '大纲设计', index: '02' },
  { id: 'preview', label: '预览', index: '03' }
]
const activeGeneratePanel = ref('settings')

const resolveAbsoluteUrl = (url) => {
  if (!url) return ''
  if (url.startsWith('http')) return url
  const base = import.meta.env.VITE_API_URL || '/api'
  if (base.startsWith('http')) {
    return new URL(url, base).toString()
  }
  return new URL(url, window.location.origin).toString()
}

const buildPreviewFileUrl = (downloadUrl) => {
  if (!downloadUrl) return ''
  try {
    if (downloadUrl.startsWith('http')) {
      return downloadUrl
    }
    const apiBase = import.meta.env.VITE_API_URL || '/api'
    const base = apiBase.startsWith('http') ? apiBase : window.location.origin
    const url = new URL(downloadUrl, base)
    const token = store.state.token
    if (token) {
      url.searchParams.set('token', token)
    }
    url.searchParams.set('inline', '1')
    url.searchParams.set('ngrok-skip-browser-warning', '1')
    return url.toString()
  } catch (error) {
    return downloadUrl
  }
}

const previewEmbedUrl = computed(() => {
  const absUrl = resolveAbsoluteUrl(previewFileUrl.value)
  if (!absUrl) return ''
  return `https://view.officeapps.live.com/op/embed.aspx?src=${encodeURIComponent(absUrl)}`
})

watchEffect(() => {
  console.log('Preview Embed URL:', previewEmbedUrl.value)
})

watchEffect(() => {
  console.log('Preview File URL:', previewFileUrl.value, 'Download URL:', previewDownloadUrl.value)
})

const hasLocalPreview = computed(() => Array.isArray(previewSlides.value) && previewSlides.value.length > 0)
const previewSlideCount = computed(() => previewSlides.value.length)
const hasPreviewFile = computed(() => Boolean(previewRequestId.value && (previewEmbedUrl.value || hasLocalPreview.value)))
const previewRequestItem = computed(() => ({
  id: previewRequestId.value,
  hasFile: hasPreviewFile.value,
  downloadUrl: previewDownloadUrl.value || (previewRequestId.value ? `/api/ppt/file?id=${previewRequestId.value}` : ''),
  downloadUrlPdf: previewDownloadUrlPdf.value || (previewRequestId.value ? `/api/ppt/file?id=${previewRequestId.value}&format=pdf` : ''),
  title: previewTitle.value || 'presentation'
}))
const currentPreviewSlide = computed(() => {
  if (!hasLocalPreview.value) return null
  const index = Math.min(Math.max(previewIndex.value, 0), previewSlides.value.length - 1)
  return previewSlides.value[index]
})
const previewLayoutClass = computed(() => {
  const slide = currentPreviewSlide.value
  if (!slide) return 'layout-default'
  const rawType = slide.layout?.type || slide.layoutHint || 'default'
  const normalized = String(rawType)
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
  return `layout-${normalized || 'default'}`
})
const currentPreviewImages = computed(() => currentPreviewSlide.value?.imageUrls || [])
const currentPreviewPrompts = computed(() => currentPreviewSlide.value?.imagePrompts || [])
const currentPreviewNotes = computed(() => currentPreviewSlide.value?.notes || '')
const currentPreviewSuggestions = computed(() => currentPreviewSlide.value?.suggestions || [])
const currentPreviewBullets = computed(() => currentPreviewSlide.value?.bullets || [])
const currentPreviewRawText = computed(() => currentPreviewSlide.value?.rawText || '')
const previewFallbackImage = computed(() => {
  const template = selectedTemplate.value
  return template?.previewImage || template?.preview_image || ''
})
const hasPreviewMedia = computed(() => {
  return currentPreviewImages.value.length > 0 ||
    currentPreviewPrompts.value.length > 0 ||
    Boolean(previewFallbackImage.value)
})
const canUseOnlinePreview = computed(() => Boolean(previewEmbedUrl.value))
const canUseLocalPreview = computed(() => hasLocalPreview.value)

const setPreviewMode = (mode) => {
  if (mode === 'online' && !canUseOnlinePreview.value) {
    return
  }
  if (mode === 'local' && !canUseLocalPreview.value) {
    return
  }
  previewMode.value = mode
}

const normalizePreviewSlide = (slide) => {
  if (!slide || typeof slide !== 'object') return null
  return {
    title: slide.title || '',
    bullets: Array.isArray(slide.bullets) ? slide.bullets : [],
    rawText: slide.rawText || slide.raw_text || '',
    imageUrls: Array.isArray(slide.imageUrls || slide.image_urls) ? (slide.imageUrls || slide.image_urls) : [],
    imagePrompts: Array.isArray(slide.imagePrompts || slide.image_prompts) ? (slide.imagePrompts || slide.image_prompts) : [],
    suggestions: Array.isArray(slide.suggestions) ? slide.suggestions : [],
    notes: slide.notes || '',
    layout: slide.layout || null,
    layoutHint: slide.layoutHint || slide.layout_hint || ''
  }
}

const normalizeOutlineItem = (item = {}) => {
  const kps = Array.isArray(item.keyPoints || item.key_points)
    ? [...(item.keyPoints || item.key_points)]
    : []
  if (kps.length === 0) kps.push('')
  return {
    title: item.title || '',
    summary: item.summary || '',
    keyPoints: kps,
    pageType: item.pageType || item.page_type || 'content'
  }
}

const outlineReady = computed(() => Array.isArray(outlineItems.value) && outlineItems.value.length > 0)
const canGoPrevPanel = computed(() => generatePanels.findIndex(p => p.id === activeGeneratePanel.value) > 0)
const canGoNextPanel = computed(() => {
  const index = generatePanels.findIndex(p => p.id === activeGeneratePanel.value)
  return index >= 0 && index < generatePanels.length - 1
})

const updateOutlineKeyPoints = (index, value) => {
  if (!outlineItems.value[index]) return
  outlineItems.value[index].keyPoints = value
    .split('\n')
    .map(item => item.trim())
    .filter(Boolean)
}

const addOutlineItem = () => {
  outlineItems.value.push({
    title: '',
    summary: '',
    keyPoints: [''],
    pageType: 'content'
  })
  // 同步 pages 字段，保持与实际大纲条数一致
  generateForm.value.pages = outlineItems.value.length
  // 自动展开新增的卡片
  outlineExpandedIndex.value = outlineItems.value.length - 1
}

const removeOutlineItem = (index) => {
  // 至少保留 1 条，防止大纲被清空为 0 导致无法继续生成
  if (outlineItems.value.length <= 1) {
    ElMessage.warning('至少保留一页大纲')
    return
  }
  outlineItems.value.splice(index, 1)
  // 同步 pages 字段，保持与实际大纲条数一致
  generateForm.value.pages = outlineItems.value.length
}

const setGeneratePanel = (panelId) => {
  if (generatePanels.find(panel => panel.id === panelId)) {
    activeGeneratePanel.value = panelId
  }
}

const goPrevPanel = () => {
  const index = generatePanels.findIndex(p => p.id === activeGeneratePanel.value)
  if (index > 0) {
    activeGeneratePanel.value = generatePanels[index - 1].id
  }
}

const goNextPanel = () => {
  const index = generatePanels.findIndex(p => p.id === activeGeneratePanel.value)
  if (index >= 0 && index < generatePanels.length - 1) {
    activeGeneratePanel.value = generatePanels[index + 1].id
  }
}

const resolveDownloadUrl = (request) => {
  if (!request) return ''
  if (request.downloadUrl) return request.downloadUrl
  if (request.id) return `/api/ppt/file?id=${request.id}`
  return ''
}

const buildDownloadUrl = (request) => {
  const raw = resolveDownloadUrl(request)
  if (!raw) return ''
  try {
    if (raw.startsWith('http')) {
      const parsed = new URL(raw)
      if (!parsed.pathname.includes('/ppt/file')) {
        return raw
      }
    }
    const apiBase = import.meta.env.VITE_API_URL || '/api'
    const base = apiBase.startsWith('http') ? apiBase : window.location.origin
    const url = new URL(raw, base)
    const token = store.state.token
    if (token) {
      url.searchParams.set('token', token)
    }
    url.searchParams.set('ngrok-skip-browser-warning', '1')
    return url.toString()
  } catch (error) {
    return raw
  }
}

const resolveDownloadUrlPdf = (request) => {
  if (!request?.id) return ''
  if (request.downloadUrlPdf) return request.downloadUrlPdf
  return `/api/ppt/file?id=${request.id}&format=pdf`
}

const buildDownloadUrlPdf = (request) => {
  const raw = resolveDownloadUrlPdf(request)
  if (!raw) return ''
  try {
    if (raw.startsWith('http')) {
      const parsed = new URL(raw)
      if (!parsed.pathname.includes('/ppt/file')) {
        return raw
      }
    }
    const apiBase = import.meta.env.VITE_API_URL || '/api'
    const base = apiBase.startsWith('http') ? apiBase : window.location.origin
    const url = new URL(raw, base)
    const token = store.state.token
    if (token) {
      url.searchParams.set('token', token)
    }
    url.searchParams.set('ngrok-skip-browser-warning', '1')
    return url.toString()
  } catch (error) {
    return raw
  }
}

const hydratePreviewFromRequest = async (request, { force = false } = {}) => {
  if (!request?.id) return
  if (!force && previewRequestId.value === request.id && (previewFileUrl.value || previewSlides.value.length)) {
    return
  }
  previewRequestId.value = request.id
  previewDownloadUrl.value = request.downloadUrl || ''
  previewDownloadUrlPdf.value = request.downloadUrlPdf || request.download_url_pdf || ''
  previewTitle.value = request.title || ''
  previewFileUrl.value = buildPreviewFileUrl(resolveDownloadUrl(request))
  previewSlides.value = []
  previewIndex.value = 0
  try {
    const response = await pptAPI.preview(request.id)
    const slides = Array.isArray(response.data?.slides)
      ? response.data.slides
      : Array.isArray(response.data?.preview)
        ? response.data.preview
        : []
    previewSlides.value = slides.map(normalizePreviewSlide).filter(Boolean)
    if (!outlineReady.value && Array.isArray(response.data?.outline)) {
      outlineItems.value = response.data.outline.map(normalizeOutlineItem)
    }
  } catch (error) {
    previewSlides.value = []
  } finally {
    syncPreviewMode()
  }
}

const applyLatestPreview = async (items, { force = false } = {}) => {
  if (!Array.isArray(items) || !items.length) {
    return
  }
  if (!force && (previewFileUrl.value || previewSlides.value.length)) {
    return
  }
  const latest =
    items.find(item => item?.hasFile && item?.downloadUrl) ||
    items.find(item => item?.hasFile) ||
    items[0]
  if (latest) {
    await hydratePreviewFromRequest(latest, { force })
  }
}

const goPreviewPrev = () => {
  if (!hasLocalPreview.value) return
  previewIndex.value = Math.max(0, previewIndex.value - 1)
}

const goPreviewNext = () => {
  if (!hasLocalPreview.value) return
  previewIndex.value = Math.min(previewSlides.value.length - 1, previewIndex.value + 1)
}

const selectPreviewSlide = (index) => {
  if (!hasLocalPreview.value) return
  if (index < 0 || index >= previewSlides.value.length) return
  previewIndex.value = index
}

const syncPreviewMode = () => {
  if (previewMode.value === 'online' && canUseOnlinePreview.value) {
    return
  }
  if (previewMode.value === 'local' && canUseLocalPreview.value) {
    return
  }
  if (canUseOnlinePreview.value) {
    previewMode.value = 'online'
  } else if (canUseLocalPreview.value) {
    previewMode.value = 'local'
  }
}

const currentUser = computed(() => store.getters.currentUser)
const userInitials = computed(() => {
  if (!currentUser.value?.username) return 'U'
  return currentUser.value.username.charAt(0).toUpperCase()
})

const baseMenuItems = [
  { id: 'dashboard', text: '仪表板', icon: DataAnalysis, path: '/main', description: '系统概览与快速操作' },
  { id: 'generate', text: '智能生成', icon: MagicStick, path: '/main/generate', description: 'GenAI智能PPT生成' },
  { id: 'materials', text: '我的材料', icon: Document, path: '/main/materials', description: '上传教学材料/论文文献，提取关键信息' },
  { id: 'templates', text: '模板中心', icon: Brush, path: '/main/templates', description: '精选PPT模板库' },
  { id: 'history', text: '历史记录', icon: Document, path: '/main/history', description: '历史生成记录管理' },
  { id: 'settings', text: '系统设置', icon: Setting, path: '/main/settings', description: '个性化系统配置' }
]

const menuItems = computed(() => [...baseMenuItems])

const resolveSection = (section) => {
  if (!section) return 'dashboard'
  const exists = baseMenuItems.find(item => item.id === section)
  return exists ? section : 'dashboard'
}

const activeMenu = ref(resolveSection(route.params.section))

watch(
  () => route.params.section,
  (section) => {
    activeMenu.value = resolveSection(section)
  }
)

const generateForm = ref({
  title: '',
  topic: '',
  pages: 10,
  style: 'business',
  generateMode: 'template',
  includeImages: true,
  includeCharts: true,
  includeNotes: false,
  enableSectionSlides: false,
  sectionSlideInterval: 4,
  themePreset: '',
  modelId: '',
  templateId: '',
  materialId: ''
})

watch(
  selectedModel,
  (modelId) => {
    if (modelId && !generateForm.value.modelId) {
      generateForm.value.modelId = modelId
    }
  },
  { immediate: true }
)

watch(
  () => futurePlan.dataset,
  (value) => {
    localStorage.setItem('futureDataset', value || '')
  }
)

watch(
  () => futurePlan.notes,
  (value) => {
    localStorage.setItem('futureNotes', value || '')
  }
)

const resolveTemplateTheme = (template) => {
  const theme = template?.theme || {}
  return {
    primary: theme.primaryColor || theme.primary_color || '#0f172a',
    secondary: theme.secondaryColor || theme.secondary_color || '#1d4ed8',
    accent: theme.accentColor || theme.accent_color || '#f97316',
    background: theme.backgroundImage || theme.background_image || ''
  }
}

const styles = [
  { id: 'business', name: '商务' },
  { id: 'academic', name: '学术' },
  { id: 'creative', name: '创意' },
  { id: 'minimal', name: '简约' }
]

const pptHistory = computed(() => store.getters.pptHistory)
const historyLoading = computed(() => store.getters.historyLoading)
const templates = computed(() => store.getters.templates || [])
const templatesLoading = computed(() => store.getters.templatesLoading)
const selectedTemplate = computed(() => {
  return templates.value.find(item => item.id === generateForm.value.templateId) || null
})
const previewCardStyle = computed(() => {
  const template = selectedTemplate.value
  const theme = resolveTemplateTheme(template)
  const themeBg = theme.background
  const fallbackBg = template?.previewImage || template?.preview_image
  const background = themeBg || fallbackBg
  const primary = theme.primary
  const secondary = theme.secondary
  const accent = theme.accent
  const style = {
    '--preview-primary': primary,
    '--preview-secondary': secondary,
    '--preview-accent': accent
  }
  if (background) {
    style.backgroundImage = `linear-gradient(135deg, rgba(15,23,42,0.82), rgba(15,23,42,0.35)), url(${background})`
    style.backgroundSize = 'cover'
    style.backgroundPosition = 'center'
  }
  return style
})

watch(
  templates,
  (list) => {
    if (generateForm.value.generateMode === 'template' && !generateForm.value.templateId && Array.isArray(list) && list.length) {
      generateForm.value.templateId = list[0].id
    }
  },
  { immediate: true }
)
watch(
  () => generateForm.value.generateMode,
  (mode) => {
    if (mode === 'style') {
      generateForm.value.templateId = ''
    } else if (mode === 'template' && templates.value.length && !generateForm.value.templateId) {
      generateForm.value.templateId = templates.value[0].id
    }
  }
)

watch(
  pptHistory,
  (items) => {
    applyLatestPreview(items)
  },
  { immediate: true }
)

const statCards = computed(() => {
  const total = pptHistory.value.length
  const totalPages = pptHistory.value.reduce((acc, item) => acc + (item.pages || 0), 0)
  const averagePages = total ? Math.round(totalPages / total) : 0
  const latestStatus = pptHistory.value.length ? pptHistory.value[0].status : 'N/A'
  return [
    { id: 'generated', icon: Document, title: '已生成PPT', value: total },
    { id: 'pages', icon: Timer, title: '平均页数', value: averagePages },
    { id: 'status', icon: Star, title: '最新状态', value: latestStatus },
    { id: 'templates', icon: RefreshRight, title: '可用模板', value: templates.value.length }
  ]
})

const activeMenuItem = computed(() => menuItems.value.find(item => item.id === activeMenu.value))

const filteredHistory = computed(() => {
  if (!historyQuery.value) {
    return pptHistory.value
  }
  return historySearchResults.value
})

const historyTotal = computed(() => filteredHistory.value.length)
const historyPageCount = computed(() => Math.max(1, Math.ceil(historyTotal.value / historyPageSize.value)))
const pagedHistory = computed(() => {
  const size = historyPageSize.value
  const start = (historyPage.value - 1) * size
  return filteredHistory.value.slice(start, start + size)
})
const showHistoryPagination = computed(() => historyTotal.value > historyPageSize.value)

const filteredTemplates = computed(() => {
  if (!templateQuery.value) {
    return templates.value
  }
  return templateSearchResults.value
})

const templatesBusy = computed(() => {
  if (templateQuery.value) {
    return templateSearchLoading.value
  }
  return templatesLoading.value
})

const templatesBusyLabel = computed(() => (templateQuery.value ? '模板搜索中...' : '模板加载中...'))

const historyBusy = computed(() => {
  if (historyQuery.value) {
    return historySearchLoading.value
  }
  return historyLoading.value
})

const historyBusyLabel = computed(() => (historyQuery.value ? '历史搜索中...' : '历史记录加载中...'))

const performTemplateSearch = async (query) => {
  const currentVersion = ++templateSearchVersion
  templateSearchLoading.value = true
  try {
    const response = await templatesAPI.list({ q: query })
    if (currentVersion !== templateSearchVersion) {
      return
    }
    templateSearchResults.value = response.data?.items || []
  } catch (error) {
    if (currentVersion !== templateSearchVersion) {
      return
    }
    const fallback = templates.value.filter(item => {
      const fields = [
        item.name || '',
        item.description || '',
        item.provider || '',
        ...(item.tags || [])
      ]
      return fields.some(field => field.toLowerCase().includes(query.toLowerCase()))
    })
    templateSearchResults.value = fallback
    ElMessage.error('模板搜索失败，请稍后重试')
  } finally {
    if (currentVersion === templateSearchVersion) {
      templateSearchLoading.value = false
    }
  }
}

const triggerTemplateSearch = () => {
  const query = templateQuery.value.trim()
  if (templateSearchTimer) {
    clearTimeout(templateSearchTimer)
    templateSearchTimer = null
  }
  if (!query) {
    templateSearchVersion += 1
    templateSearchResults.value = []
    templateSearchLoading.value = false
    return
  }
  performTemplateSearch(query)
}

const performHistorySearch = async (query) => {
  const currentVersion = ++historySearchVersion
  historySearchLoading.value = true
  try {
    const items = await store.dispatch('searchPptHistory', query)
    if (currentVersion !== historySearchVersion) {
      return
    }
    historySearchResults.value = items
  } catch (error) {
    if (currentVersion !== historySearchVersion) {
      return
    }
    const fallback = pptHistory.value.filter(item => {
      const title = item.title?.toLowerCase() || ''
      const topic = item.topic?.toLowerCase() || ''
      return title.includes(query.toLowerCase()) || topic.includes(query.toLowerCase())
    })
    historySearchResults.value = fallback
    ElMessage.error('历史搜索失败，请稍后重试')
  } finally {
    if (currentVersion === historySearchVersion) {
      historySearchLoading.value = false
    }
  }
}

const triggerHistorySearch = () => {
  const query = historyQuery.value.trim()
  if (historySearchTimer) {
    clearTimeout(historySearchTimer)
    historySearchTimer = null
  }
  if (!query) {
    historySearchVersion += 1
    historySearchResults.value = []
    historySearchLoading.value = false
    return
  }
  historyPage.value = 1
  performHistorySearch(query)
}

const handleHistoryPageChange = (page) => {
  historyPage.value = page
}

const handleHistorySizeChange = (size) => {
  historyPageSize.value = size
  historyPage.value = 1
}

watch(
  historyQuery,
  (value) => {
    const query = value.trim()
    historyPage.value = 1
    if (!query) {
      historySearchResults.value = []
      historySearchLoading.value = false
      if (historySearchTimer) {
        clearTimeout(historySearchTimer)
        historySearchTimer = null
      }
      return
    }
    if (historySearchTimer) {
      clearTimeout(historySearchTimer)
    }
    historySearchTimer = setTimeout(() => {
      performHistorySearch(query)
    }, 300)
  }
)

watch(
  [historyTotal, historyPageSize],
  () => {
    if (!historyTotal.value) {
      historyPage.value = 1
      return
    }
    const maxPage = historyPageCount.value
    if (historyPage.value > maxPage) {
      historyPage.value = maxPage
    }
  }
)

watch(
  templateQuery,
  (value) => {
    const query = value.trim()
    if (!query) {
      templateSearchResults.value = []
      templateSearchLoading.value = false
      if (templateSearchTimer) {
        clearTimeout(templateSearchTimer)
        templateSearchTimer = null
      }
      return
    }
    if (templateSearchTimer) {
      clearTimeout(templateSearchTimer)
    }
    templateSearchTimer = setTimeout(() => {
      performTemplateSearch(query)
    }, 300)
  }
)

onBeforeUnmount(() => {
  if (templateSearchTimer) {
    clearTimeout(templateSearchTimer)
    templateSearchTimer = null
  }
  if (historySearchTimer) {
    clearTimeout(historySearchTimer)
    historySearchTimer = null
  }
})

const formatDate = (value) => {
  if (!value) return '未知'
  const raw = String(value).trim()
  if (/^\d+$/.test(raw)) {
    const numeric = Number(raw)
    const ts = numeric < 1e12 ? numeric * 1000 : numeric
    return dayjs(ts).format('YYYY/MM/DD HH:mm')
  }
  const parsed = dayjs(raw)
  if (parsed.isValid()) {
    const year = parsed.year()
    if (year > 3000) {
      return raw.replace(/-/g, '/').replace('T', ' ').replace('Z', '').slice(0, 16)
    }
    return parsed.format('YYYY/MM/DD HH:mm')
  }
  if (/^\d{4}-\d{2}-\d{2}\s\d{2}:\d{2}/.test(raw)) {
    return raw.replace(/-/g, '/').slice(0, 16)
  }
  return raw
}

const historySourceDisplay = (item) => {
  const name = item?.templateName || item?.template_name || ''
  if (!name) return '模板: 未选择'
  if (name.startsWith('预设主题:')) return name
  if (name.startsWith('风格:')) return '预设主题:' + name.slice(3)
  return '模板: ' + name
}

const resetGenerateForm = () => {
  const keepTemplateId = generateForm.value.templateId || templates.value[0]?.id || ''
  generateForm.value = {
    title: '',
    topic: '',
    pages: 10,
    style: 'business',
    generateMode: 'template',
    includeImages: true,
    includeCharts: true,
    includeNotes: false,
    enableSectionSlides: false,
    sectionSlideInterval: 4,
    themePreset: '',
    modelId: selectedModel.value || 'qwen-turbo',
    templateId: keepTemplateId,
    materialId: ''
  }
  outlineItems.value = []
  activeGeneratePanel.value = 'settings'
  generateSource.value = 'manual'
}

const openGeneratePanel = () => {
  activeMenu.value = 'generate'
  activeGeneratePanel.value = 'settings'
  if (route.params.section !== 'generate') {
    router.push('/main/generate')
  }
  requestAnimationFrame(() => {
    const target = document.querySelector('.generate-section')
    if (target) {
      target.scrollIntoView({ behavior: 'smooth', block: 'start' })
    }
  })
}

const handleGenerateOutline = async () => {
  if (!generateForm.value.title.trim() || !generateForm.value.topic.trim()) {
    ElMessage.warning('请填写标题和主题描述')
    return
  }
  if (generateForm.value.generateMode === 'template' && !generateForm.value.templateId) {
    ElMessage.warning('请选择模板')
    return
  }
  // 已有大纲时提示用户确认，防止误覆盖手动编辑内容
  if (outlineReady.value) {
    try {
      await ElMessageBox.confirm(
        '重新生成将覆盖当前所有已编辑的大纲内容，确定继续吗？',
        '重新生成确认',
        { confirmButtonText: '确定重新生成', cancelButtonText: '取消', type: 'warning' }
      )
    } catch {
      return
    }
  }
  outlineLoading.value = true
  try {
    const templateIdForOutline = generateForm.value.generateMode === 'template'
      ? generateForm.value.templateId
      : (templates.value[0]?.id || '')
    const payload = {
      title: generateForm.value.title.trim(),
      topic: generateForm.value.topic.trim(),
      pages: generateForm.value.pages,
      style: generateForm.value.style,
      modelId: generateForm.value.modelId || selectedModel.value,
      templateId: templateIdForOutline,
      generateMode: generateForm.value.generateMode,
      materialId: generateForm.value.materialId || ''
    }
    const response = await pptAPI.outline(payload)
    const outline = Array.isArray(response.data?.outline) ? response.data.outline : []
    outlineItems.value = outline.map(normalizeOutlineItem)
    if (outlineItems.value.length) {
      activeGeneratePanel.value = 'outline'
      outlineExpandedIndex.value = 0
    }
    if (!outlineItems.value.length) {
      ElMessage.warning('未生成大纲内容')
    }
  } catch (error) {
    // 拦截器已统一 ElMessage
    console.error('大纲生成失败:', error.userMessage || error.response?.data?.message)
  } finally {
    outlineLoading.value = false
  }
}

const handleGenerate = async () => {
  if (!generateForm.value.title.trim() || !generateForm.value.topic.trim()) {
    ElMessage.warning('请填写标题和主题描述')
    return
  }
  if (generateForm.value.generateMode === 'template' && !generateForm.value.templateId) {
    ElMessage.warning('请选择模板')
    return
  }
  if (!outlineReady.value) {
    ElMessage.warning('请先生成并确认大纲')
    return
  }

  generating.value = true
  previewFileUrl.value = ''
  previewSlides.value = []
  previewIndex.value = 0
  try {
    const payload = {
      title: generateForm.value.title.trim(),
      topic: generateForm.value.topic.trim(),
      pages: generateForm.value.pages,
      style: generateForm.value.style,
      generateMode: generateForm.value.generateMode,
      includeImages: generateForm.value.includeImages,
      includeCharts: generateForm.value.includeCharts,
      includeNotes: generateForm.value.includeNotes,
      enableSectionSlides: !!generateForm.value.enableSectionSlides,
      sectionSlideInterval: Math.min(10, Math.max(2, generateForm.value.sectionSlideInterval || 4)),
      themePreset: generateForm.value.generateMode === 'style' ? (generateForm.value.themePreset || '').trim() : '',
      modelId: generateForm.value.modelId || selectedModel.value,
      templateId: generateForm.value.generateMode === 'template' ? generateForm.value.templateId : '',
      outline: outlineItems.value.map(item => ({
        title: item.title || '',
        summary: item.summary || '',
        key_points: (item.keyPoints || []).filter(kp => kp.trim() !== '')
      })),
      materialId: generateForm.value.materialId || ''
    }
    const result = await store.dispatch('createPptRequest', payload)
    if (!result?.request) {
      throw new Error('生成请求失败')
    }
    previewDownloadUrl.value = result.request?.downloadUrl || ''
    previewDownloadUrlPdf.value = result.request?.downloadUrlPdf || result.request?.download_url_pdf || ''
    previewTitle.value = result.request?.title || ''
    previewFileUrl.value = buildPreviewFileUrl(resolveDownloadUrl(result.request))
    previewSlides.value = Array.isArray(result.preview)
      ? result.preview.map(normalizePreviewSlide).filter(Boolean)
      : []
    previewIndex.value = 0
    previewRequestId.value = result.request?.id || 0
    if (!previewSlides.value.length && result.request?.id) {
      await hydratePreviewFromRequest(result.request, { force: true })
    } else {
      syncPreviewMode()
    }
    await store.dispatch('fetchPptHistory')
    activeMenu.value = 'generate'
    activeGeneratePanel.value = 'preview'
  } catch (error) {
    const msg = error?.message || error?.userMessage || error?.response?.data?.message || '生成失败，请稍后重试'
    ElMessage.error(msg)
    console.error('生成失败:', msg)
  } finally {
    generating.value = false
  }
}

const editPPT = (item) => {
  if (!item?.id) {
    ElMessage.warning('无法识别该记录的 ID')
    return
  }
  router.push(`/main/edit/${item.id}`)
}

const downloadPPT = (item) => {
  if (!item?.hasFile) {
    ElMessage.warning('该记录暂无可下载的PPT文件')
    return
  }
  const url = buildDownloadUrl(item)
  if (!url) {
    ElMessage.error('下载链接不可用，请稍后重试')
    return
  }
  const link = document.createElement('a')
  link.href = url
  link.rel = 'noopener'
  link.target = '_blank'
  link.download = ''
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
}

const downloadPDF = (item) => {
  if (!item?.hasFile) {
    ElMessage.warning('该记录暂无可下载的PPT文件')
    return
  }
  const url = buildDownloadUrlPdf(item)
  if (!url) {
    ElMessage.error('下载链接不可用，请稍后重试')
    return
  }
  const link = document.createElement('a')
  link.href = url
  link.rel = 'noopener'
  link.target = '_blank'
  link.download = (item?.title || 'presentation') + '.pdf'
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
}

const deleteHistory = async (item) => {
  if (!item?.id) {
    return
  }
  try {
    await ElMessageBox.confirm(
      `确定删除《${item.title || '未命名PPT'}》吗？`,
      '删除确认',
      {
        confirmButtonText: '删除',
        cancelButtonText: '取消',
        type: 'warning'
      }
    )
    await store.dispatch('deletePptRequest', item.id)
    if (historyQuery.value) {
      historySearchResults.value = historySearchResults.value.filter(entry => entry.id !== item.id)
    }
    ElMessage.success('删除成功')
  } catch (error) {
    if (error === 'cancel' || error === 'close' || error?.message === 'cancel') {
      return
    }
    // 拦截器已统一 ElMessage
    console.error('删除失败:', error.userMessage || error.response?.data?.message)
  }
}

const useTemplate = (template) => {
  generateForm.value.title = template.name || '自定义PPT'
  if (template.description) {
    generateForm.value.topic = template.description
  }
  generateForm.value.modelId = selectedModel.value || generateForm.value.modelId
  if (template.id) {
    generateForm.value.templateId = template.id
  }
  openGeneratePanel()
}

const handleModelChange = (modelId) => {
  store.dispatch('updateDefaultModel', modelId)
  generateForm.value.modelId = modelId
}

// ---- 文档材料功能 ----

const stopMaterialPolling = () => {
  if (materialPollingTimer.value) {
    clearInterval(materialPollingTimer.value)
    materialPollingTimer.value = null
  }
}

const startMaterialPolling = (id) => {
  stopMaterialPolling()
  materialPollingTimer.value = setInterval(async () => {
    try {
      const res = await materialAPI.getStatus(id)
      const mat = res.data?.material
      if (!mat) return
      materialStatus.value = mat.status
      if (mat.status === 'completed') {
        stopMaterialPolling()
        const resultRes = await materialAPI.getResult(id)
        materialExtractResult.value = resultRes.data?.material?.extractResult || null
        ElMessage.success('文档提取完成，可查看提取结果')
      } else if (mat.status === 'failed') {
        stopMaterialPolling()
        ElMessage.error('文档提取失败：' + (mat.errorMsg || '未知错误'))
      }
    } catch (e) {
      console.error('轮询提取状态失败', e)
    }
  }, 2500)
}

const handleMaterialFileChange = (e) => {
  const file = e.target.files?.[0]
  if (!file) return
  const allowed = ['pdf', 'docx', 'txt']
  const ext = file.name.split('.').pop()?.toLowerCase()
  if (!allowed.includes(ext)) {
    ElMessage.warning('仅支持 PDF、DOCX、TXT 格式')
    e.target.value = ''
    return
  }
  if (file.size > 20 * 1024 * 1024) {
    ElMessage.warning('文件大小不能超过 20MB')
    e.target.value = ''
    return
  }
  materialUploadFile.value = file
}

const handleMaterialUpload = async () => {
  if (!materialUploadFile.value) {
    ElMessage.warning('请先选择文件')
    return
  }
  materialUploading.value = true
  materialUploadProgress.value = 0
  materialStatus.value = ''
  materialExtractResult.value = null
  materialCurrentId.value = ''
  try {
    const res = await materialAPI.upload(materialUploadFile.value, (pct) => {
      materialUploadProgress.value = pct
    })
    const mat = res.data?.material
    if (!mat?.id) throw new Error('上传失败')
    materialCurrentId.value = mat.id
    materialStatus.value = mat.status
    ElMessage.success('上传成功，正在提取关键信息...')
    startMaterialPolling(mat.id)
  } catch (e) {
    ElMessage.error('上传失败：' + (e?.response?.data?.message || e?.message || '未知错误'))
  } finally {
    materialUploading.value = false
  }
}

const useMaterialForGenerate = () => {
  if (!materialExtractResult.value) return
  const er = materialExtractResult.value
  if (er.title) generateForm.value.title = er.title
  if (er.summary) generateForm.value.topic = er.summary
  if (er.outline?.length) {
    generateForm.value.topic = (er.summary || '') + '\n主要章节：' + er.outline.join('；')
  }
  generateForm.value.materialId = materialCurrentId.value
  generateSource.value = 'manual'
  ElMessage.success('已填充提取内容，请确认后继续生成')
}

const loadMaterialsList = async () => {
  materialsLoading.value = true
  try {
    const res = await materialAPI.list()
    materialsList.value = res.data?.materials || []
  } catch (e) {
    ElMessage.error('加载材料列表失败')
  } finally {
    materialsLoading.value = false
  }
}

const deleteMaterial = async (id, filename) => {
  try {
    await ElMessageBox.confirm(`确定删除《${filename}》吗？`, '删除确认', {
      confirmButtonText: '删除', cancelButtonText: '取消', type: 'warning'
    })
    await materialAPI.remove(id)
    materialsList.value = materialsList.value.filter(m => m.id !== id)
    ElMessage.success('删除成功')
  } catch (e) {
    if (e === 'cancel' || e === 'close' || e?.message === 'cancel') return
    ElMessage.error('删除失败')
  }
}

const viewMaterialDetail = async (id) => {
  materialDetailId.value = id
  materialDetailData.value = null
  materialDetailVisible.value = true
  try {
    const res = await materialAPI.getResult(id)
    materialDetailData.value = res.data?.material || null
  } catch (e) {
    ElMessage.error('加载提取结果失败')
  }
}

const useMaterialFromList = (mat) => {
  if (!mat?.extractResult) {
    ElMessage.warning('该材料尚未提取完成')
    return
  }
  const er = mat.extractResult
  if (er.title) generateForm.value.title = er.title
  if (er.summary) generateForm.value.topic = er.summary
  if (er.outline?.length) {
    generateForm.value.topic = (er.summary || '') + '\n主要章节：' + er.outline.join('；')
  }
  generateForm.value.materialId = mat.id
  materialDetailVisible.value = false
  activeMenu.value = 'generate'
  router.push('/main/generate')
  ElMessage.success('已填充提取内容，请确认后继续生成')
}

const materialStatusLabel = (status) => {
  const map = { pending: '等待提取', extracting: '提取中...', completed: '提取完成', failed: '提取失败' }
  return map[status] || status
}

const materialStatusType = (status) => {
  const map = { pending: 'info', extracting: 'warning', completed: 'success', failed: 'danger' }
  return map[status] || 'info'
}

watch(
  () => activeMenu.value,
  (val) => {
    if (val === 'materials') loadMaterialsList()
  }
)

onBeforeUnmount(() => {
  stopMaterialPolling()
})

const handleLogout = async () => {
  try {
    await store.dispatch('logout')
  } catch (error) {
    console.error('退出登录失败:', error)
  } finally {
  previewFileUrl.value = ''
  previewSlides.value = []
  previewIndex.value = 0
  previewMode.value = 'online'
  previewRequestId.value = 0
  previewDownloadUrl.value = ''
  previewDownloadUrlPdf.value = ''
  previewTitle.value = ''
    outlineItems.value = []
    ElMessage.success('已退出登录')
    await new Promise(resolve => setTimeout(resolve, 500))
    router.push('/login')
  }
}

const ensureSession = async () => {
  try {
    if (!store.state.user) {
      await store.dispatch('fetchCurrentUser')
    }
  } catch (error) {
    if (error?.response?.status === 401) {
      router.push('/login')
      return
    }
    console.error('加载用户数据失败:', error)
    ElMessage.warning('用户信息加载失败，将继续尝试加载数据')
  }

  const tasks = [
    store.dispatch('fetchPptHistory'),
    store.dispatch('fetchTemplates'),
    store.dispatch('fetchModels')
  ]
  if (store.state.user?.isAdmin) {
    tasks.push(store.dispatch('fetchAdminHistory'))
  }
  const results = await Promise.allSettled(tasks)
  results.forEach((result) => {
    if (result.status === 'rejected') {
      const status = result.reason?.response?.status
      if (status === 401) {
        router.push('/login')
      } else {
        console.error('加载数据失败:', result.reason)
      }
    }
  })
  const historyResult = results[0]
  if (historyResult?.status === 'fulfilled') {
    await applyLatestPreview(historyResult.value, { force: true })
  }
}

onMounted(() => {
  if (!store.state.isAuthenticated) {
    router.push('/login')
    return
  }
  ensureSession()
})
</script>

<style scoped>
@import url('https://fonts.googleapis.com/css2?family=Noto+Sans+SC:wght@400;500;600;700&family=Space+Grotesk:wght@500;600;700&display=swap');

.main-container {
  font-family: 'Plus Jakarta Sans', 'Noto Sans SC', 'PingFang SC', sans-serif;
  background: radial-gradient(circle at 15% 20%, rgba(224, 242, 254, 0.9), transparent 35%),
    radial-gradient(circle at 85% 0%, rgba(186, 230, 253, 0.5), transparent 35%),
    linear-gradient(120deg, #f8fafc 0%, #f0f9ff 100%);
  position: relative;
  overflow: hidden;
}

.main-container::before {
  content: '';
  position: absolute;
  inset: 0;
  background-image: radial-gradient(circle at 1px 1px, rgba(15, 23, 42, 0.05) 1px, transparent 0);
  background-size: 32px 32px;
  pointer-events: none;
  opacity: 0.6;
}

.main-container > * {
  position: relative;
  z-index: 1;
}

h1,
h2,
h3,
h4 {
  font-family: 'Space Grotesk', 'Noto Sans SC', sans-serif;
  letter-spacing: -0.02em;
}
.main-container {
  display: flex;
  min-height: 100vh;
}

.sidebar {
  width: 280px;
  background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%);
  color: white;
  display: flex;
  flex-direction: column;
  box-shadow: 5px 0 15px rgba(0, 0, 0, 0.1);
}

.sidebar-header {
  padding: 30px 20px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.sidebar-header h2 {
  font-size: 1.5rem;
  margin-bottom: 20px;
  font-weight: 600;
}

.user-info {
  display: flex;
  align-items: center;
  gap: 12px;
}

.avatar {
  width: 50px;
  height: 50px;
  background: linear-gradient(135deg, #0EA5E9 0%, #38BDF8 100%);
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.2rem;
  font-weight: bold;
}

.user-details {
  flex: 1;
}

.username {
  font-weight: 600;
  font-size: 1rem;
}

.user-email {
  font-size: 0.875rem;
  opacity: 0.8;
}

.sidebar-nav {
  flex: 1;
  padding: 20px 0;
}

.nav-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 15px 25px;
  color: rgba(255, 255, 255, 0.8);
  text-decoration: none;
  transition: all 0.3s;
  border-left: 4px solid transparent;
}

.nav-item:hover {
  background: rgba(255, 255, 255, 0.1);
  color: white;
  border-left-color: #0EA5E9;
}

.nav-item.active {
  background: rgba(14, 165, 233, 0.2);
  color: white;
  border-left-color: #0EA5E9;
}

.nav-icon {
  font-size: 1.2rem;
}

.nav-text {
  font-size: 1rem;
  font-weight: 500;
}

.sidebar-footer {
  padding: 20px;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.admin-entry {
  display: flex;
  align-items: center;
  gap: 10px;
  width: 100%;
  padding: 12px;
  background: rgba(14, 165, 233, 0.18);
  color: #bae6fd;
  border: 1px solid rgba(56, 189, 248, 0.5);
  border-radius: 10px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s ease;
}

.admin-entry:hover {
  background: rgba(14, 165, 233, 0.3);
  border-color: rgba(56, 189, 248, 0.9);
  color: #e0f2fe;
}

.admin-entry-icon {
  font-size: 1.05rem;
}

.logout-btn {
  display: flex;
  align-items: center;
  gap: 10px;
  width: 100%;
  padding: 12px;
  background: rgba(239, 68, 68, 0.1);
  color: #ef4444;
  border: none;
  border-radius: 10px;
  cursor: pointer;
  transition: all 0.3s;
}

.logout-btn:hover {
  background: rgba(239, 68, 68, 0.2);
}

.main-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.main-header {
  background: var(--color-bg-card);
  padding: var(--space-lg) var(--space-xl);
  border-bottom: 1px solid var(--color-border);
  display: flex;
  justify-content: space-between;
  align-items: center;
  box-shadow: var(--shadow-nav);
}

.header-right {
  display: flex;
  gap: var(--space-sm);
  align-items: center;
  flex-wrap: wrap;
}

.header-left h1 {
  font-size: var(--text-title-page);
  font-weight: 600;
  color: var(--color-text);
  margin-bottom: var(--space-xs);
  line-height: var(--line-height-tight);
}

.page-description {
  color: var(--color-text-muted);
  font-size: var(--text-caption);
}

.generate-btn {
  display: flex;
  align-items: center;
  gap: var(--space-xs);
  padding: var(--space-sm) var(--space-lg);
  background: linear-gradient(135deg, #0EA5E9 0%, #38BDF8 100%);
  color: white;
  border: none;
  border-radius: var(--radius-btn);
  font-size: var(--text-body);
  font-weight: 600;
  cursor: pointer;
  transition: transform var(--transition-default), box-shadow var(--transition-default);
}

.generate-btn:hover {
  transform: translateY(-2px);
  box-shadow: var(--shadow-card-hover);
}

.content-area {
  flex: 1;
  padding: var(--space-xl);
  overflow-y: auto;
}

.dashboard {
  display: flex;
  flex-direction: column;
  gap: var(--space-xl);
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: var(--space-lg);
}

.stat-card {
  background: var(--color-bg-card);
  border-radius: var(--radius-card);
  padding: var(--space-lg);
  box-shadow: var(--shadow-card);
  border: 1px solid rgba(0, 0, 0, 0.06);
  display: flex;
  align-items: center;
  gap: var(--space-lg);
  transition: box-shadow var(--transition-default), transform var(--transition-default);
  cursor: default;
}

.stat-card:hover {
  box-shadow: var(--shadow-card-hover);
  transform: translateY(-2px);
}

.stat-icon {
  font-size: 2.5rem;
  color: var(--color-primary, #0EA5E9);
  opacity: 0.9;
}

.stat-content h3 {
  font-size: var(--text-caption);
  color: var(--color-text-muted);
  margin-bottom: var(--space-xs);
  font-weight: 500;
}

.stat-number {
  font-size: var(--text-kpi);
  font-weight: 700;
  color: var(--color-text);
  line-height: var(--line-height-tight);
}

.features-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: var(--space-lg);
}

.feature-card {
  background: var(--color-bg-card);
  border-radius: var(--radius-card);
  padding: var(--space-lg);
  box-shadow: var(--shadow-card);
  border: 1px solid rgba(0, 0, 0, 0.06);
  text-align: center;
  transition: box-shadow var(--transition-default), transform var(--transition-default);
}

.feature-card:hover {
  box-shadow: var(--shadow-card-hover);
  transform: translateY(-2px);
}

.feature-icon {
  font-size: 2.5rem;
  color: var(--color-primary, #0EA5E9);
  margin-bottom: var(--space-md);
}

.feature-card h4 {
  font-size: var(--text-card);
  font-weight: 600;
  margin-bottom: var(--space-sm);
  color: var(--color-text);
}

.feature-card p {
  color: var(--color-text-muted);
  margin-bottom: var(--space-lg);
  font-size: var(--text-caption);
  line-height: var(--line-height-body);
}

.feature-action {
  padding: var(--space-sm) var(--space-lg);
  background: var(--color-bg-page-alt);
  border: none;
  border-radius: var(--radius-btn);
  color: var(--color-primary);
  font-weight: 500;
  cursor: pointer;
  transition: background var(--transition-default), color var(--transition-default);
}

.feature-action:hover {
  background: rgba(14, 165, 233, 0.12);
}

.history-section {
  background: var(--color-bg-card);
  border-radius: var(--radius-card);
  padding: var(--space-lg);
  box-shadow: var(--shadow-card);
  border: 1px solid rgba(0, 0, 0, 0.06);
}


.section-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: var(--space-xl);
}

.search-box input {
  padding: var(--space-sm) var(--space-md);
  border: 2px solid var(--color-border);
  border-radius: var(--radius-input);
  width: 300px;
}

.template-search-box {
  display: flex;
  align-items: center;
  gap: 10px;
}

.template-search-box :deep(.el-input__wrapper) {
  border: 2px solid var(--color-border);
  border-radius: var(--radius-input);
  box-shadow: none;
  padding: 0 var(--space-sm);
  width: 300px;
}

.template-search-box :deep(.el-input__wrapper.is-focus) {
  border-color: #0EA5E9;
}

.template-search-box :deep(.el-input__inner) {
  height: 42px;
  font-size: 1rem;
}

.template-search-box :deep(.el-input__prefix-inner svg) {
  width: 18px;
  height: 18px;
}

.search-icon-btn {
  width: 42px;
  height: 42px;
}

.history-search-box {
  display: flex;
  align-items: center;
  gap: 10px;
}


.history-search-box :deep(.el-input__wrapper) {
  border: 2px solid var(--color-border);
  border-radius: var(--radius-input);
  box-shadow: none;
  padding: 0 var(--space-sm);
  width: 300px;
}

.history-search-box :deep(.el-input__wrapper.is-focus) {
  border-color: #0EA5E9;
}

.history-search-box :deep(.el-input__inner) {
  height: 42px;
  font-size: 1rem;
}

.history-search-box :deep(.el-input__prefix-inner svg) {
  width: 18px;
  height: 18px;
}

.history-list {
  display: flex;
  flex-direction: column;
  gap: 15px;
}

.history-pagination {
  display: flex;
  justify-content: center;
  margin-top: 20px;
}


.history-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  transition: all 0.3s;
}

.history-item:hover {
  border-color: #0EA5E9;
  box-shadow: 0 5px 15px rgba(14, 165, 233, 0.1);
}

.history-preview {
  display: flex;
  align-items: center;
  gap: 15px;
}

.preview-icon {
  font-size: 2rem;
}

.preview-content h4 {
  font-size: 1.1rem;
  margin-bottom: 5px;
  color: #1e293b;
}

.preview-content p {
  color: #64748b;
  margin-bottom: 10px;
}

.history-meta {
  display: flex;
  gap: 20px;
  font-size: 0.875rem;
  color: #94a3b8;
  flex-wrap: wrap;
}

.history-actions {
  display: flex;
  gap: 10px;
}

.history-empty {
  padding: 40px 20px;
  text-align: center;
  border: 2px dashed #e2e8f0;
  border-radius: 12px;
  color: #94a3b8;
  font-size: 1rem;
}

.templates-empty {
  padding: 40px 20px;
  text-align: center;
  border: 2px dashed #e2e8f0;
  border-radius: 12px;
  color: #94a3b8;
  font-size: 1rem;
}

.generate-section {
  background: white;
  border-radius: 16px;
  padding: 24px;
}

.generate-panel h2 {
  margin-bottom: 8px;
}

.template-hint {
  margin-top: 8px;
  font-size: 0.9rem;
  color: #475569;
  display: flex;
  align-items: center;
  gap: 16px;
}

.template-download-link {
  font-size: 0.85rem;
  color: #2563eb;
  text-decoration: underline;
}

.preview-card {
  margin-top: 16px;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  padding: 20px;
  background: #f8fafc;
  --preview-primary: #0f172a;
  --preview-secondary: #475569;
  --preview-accent: #38BDF8;
}

.preview-empty {
  margin-top: 16px;
  padding: 24px;
  text-align: center;
  border: 2px dashed #e2e8f0;
  border-radius: 12px;
  color: #94a3b8;
  background: #f8fafc;
}

.preview-card.preview-has-bg {
  color: #f8fafc;
  border-color: rgba(255, 255, 255, 0.25);
  box-shadow: 0 25px 60px rgba(15, 23, 42, 0.4);
}

.preview-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.preview-label {
  font-size: 0.9rem;
  font-weight: 600;
  color: var(--preview-accent);
}

.preview-actions {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.preview-mode {
  display: flex;
  gap: 8px;
}

.preview-download-btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
}
.preview-download-btn .btn-icon {
  margin-right: 2px;
}

.preview-toggle {
  padding: 6px 12px;
  border-radius: 999px;
  border: 1px solid #cbd5f5;
  background: #ffffff;
  color: #334155;
  font-size: 0.85rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s ease;
}

.preview-toggle.active {
  background: #eef2ff;
  border-color: #0EA5E9;
  color: #312e81;
}

.preview-toggle:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.preview-counter {
  font-size: 0.9rem;
  color: var(--preview-secondary);
}

.preview-body h3 {
  margin-bottom: 10px;
  color: var(--preview-primary);
}

.preview-card.preview-has-bg .preview-body h3 {
  color: #f8fafc;
}

.preview-card ul {
  margin-top: 12px;
  padding-left: 20px;
}

.preview-card li {
  margin-bottom: 6px;
  color: var(--preview-primary);
}

.preview-raw {
  margin-top: 12px;
  color: var(--preview-primary);
  line-height: 1.6;
}

.preview-placeholder {
  margin-top: 12px;
  color: #94a3b8;
  font-style: italic;
}

.preview-embed {
  margin-top: 8px;
}

.preview-iframe {
  width: 100%;
  height: 520px;
  border: none;
  border-radius: 10px;
  background: #ffffff;
}

.btn-content {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.btn-icon {
  font-size: 1rem;
  line-height: 1;
}

.preview-card.preview-has-bg .preview-placeholder {
  color: rgba(248, 250, 252, 0.8);
}

.image-prompts {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
  margin-top: 12px;
}

.image-prompts span {
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 0.75rem;
  background: rgba(99, 102, 241, 0.12);
  border: 1px solid var(--preview-accent);
  color: var(--preview-accent);
}

.preview-card.preview-has-bg .image-prompts span {
  background: rgba(255, 255, 255, 0.16);
  color: #e0e7ff;
  border-color: rgba(255, 255, 255, 0.4);
}

.layout-grid {
  display: grid;
  grid-template-columns: 3fr 2fr;
  gap: 20px;
  align-items: stretch;
}

.layout-default .layout-grid,
.layout-cover .layout-grid {
  grid-template-columns: 1fr;
}

.layout-two-column .layout-grid,
.layout-story .layout-grid,
.layout-map .layout-grid,
.layout-stats .layout-grid {
  grid-template-columns: 3fr 2fr;
}

.layout-text {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.layout-media {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.layout-media .preview-images {
  margin-top: 0;
}

.layout-cover .layout-text h3 {
  font-size: 2rem;
  letter-spacing: 1px;
  text-transform: uppercase;
}

.preview-images {
  margin-top: 16px;
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.preview-images img {
  width: 200px;
  height: 120px;
  object-fit: cover;
  border-radius: 10px;
  box-shadow: 0 5px 15px rgba(15, 23, 42, 0.1);
  border: 1px solid #e2e8f0;
}

.preview-template-fallback {
  margin-top: 16px;
}

.preview-template-fallback img {
  width: 240px;
  border-radius: 12px;
  border: 1px solid #e2e8f0;
}

.preview-controls {
  display: flex;
  gap: 12px;
  margin-top: 16px;
}

.preview-nav {
  flex: 1;
  padding: 10px 16px;
  border: 1px solid #cbd5f5;
  border-radius: 10px;
  background: white;
  font-weight: 600;
  color: #334155;
  cursor: pointer;
  transition: background 0.2s, color 0.2s;
}

.preview-nav:hover:not(:disabled) {
  background: #eef2ff;
}

.preview-nav:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.preview-thumbnails {
  margin-top: 16px;
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.preview-thumb {
  display: flex;
  align-items: center;
  gap: 6px;
  border: 1px solid #e2e8f0;
  border-radius: 999px;
  padding: 6px 12px;
  background: white;
  font-size: 0.85rem;
  color: #475569;
  cursor: pointer;
  transition: all 0.2s ease;
}

.preview-thumb .thumb-index {
  font-weight: 600;
  color: #0EA5E9;
}

.preview-thumb.active {
  border-color: #0EA5E9;
  background: #eef2ff;
  color: #312e81;
}

.preview-thumb.active .thumb-index {
  color: #312e81;
}

.settings-section {
  background: white;
  border-radius: 16px;
  padding: 30px;
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.settings-card {
  background: #fff;
}

.settings-card h2 {
  margin-bottom: 8px;
}

.settings-desc {
  color: #64748b;
  margin-bottom: 20px;
}

.settings-block {
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  padding: 20px;
  margin-bottom: 16px;
}

.settings-models {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-top: 12px;
}

.settings-model {
  display: flex;
  gap: 12px;
  border: 1px solid #e5e7eb;
  border-radius: 10px;
  padding: 12px;
  cursor: pointer;
}

.settings-model.selected {
  border-color: #0EA5E9;
  background: #f3f4ff;
}

.settings-model input {
  margin-top: 6px;
}

.model-texts strong {
  display: block;
  font-size: 1rem;
  color: #111827;
}

.model-texts small {
  display: block;
  color: #6b7280;
}

.model-texts p {
  margin: 6px 0;
  color: #475569;
}

.model-tags span {
  margin-right: 12px;
  font-size: 0.85rem;
  color: #0EA5E9;
}

.plan-fields {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin: 12px 0;
}

.plan-fields input,
.plan-fields textarea {
  width: 100%;
  margin-top: 6px;
  padding: 10px 12px;
  border: 1px solid #cbd5f5;
  border-radius: 8px;
}

.templates-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 25px;
}

.template-card {
  background: white;
  border-radius: 15px;
  overflow: hidden;
  box-shadow: 0 5px 15px rgba(0, 0, 0, 0.05);
  transition: transform 0.3s;
}

.template-card:hover {
  transform: translateY(-5px);
}

.template-preview {
  height: 200px;
  background: #f1f5f9;
  display: flex;
  align-items: center;
  justify-content: center;
}

.template-preview img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  border-radius: 8px;
}

.default-preview {
  width: 80%;
}

.preview-slides {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.preview-slides .slide {
  height: 30px;
  background: white;
  border-radius: 5px;
  box-shadow: 0 2px 5px rgba(0, 0, 0, 0.1);
}

.template-info {
  padding: 20px;
}

.template-title-row {
  display: flex;
  align-items: center;
  gap: 10px;
}

.template-info h4 {
  font-size: 1.1rem;
  margin-bottom: 10px;
  color: #1e293b;
}

.template-badge {
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 0.75rem;
  background: #e0f2fe;
  color: #0369a1;
  border: 1px solid #7dd3fc;
  white-space: nowrap;
}

.template-desc {
  color: #475569;
  line-height: 1.5;
  margin-bottom: 12px;
}

.template-meta {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-size: 0.9rem;
  color: #64748b;
  margin-bottom: 10px;
}

.template-meta a {
  color: #0EA5E9;
}

.template-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 15px;
}

.template-tag {
  background: #eef2ff;
  color: #4338ca;
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 0.8rem;
}

.template-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.use-template-btn {
  flex: 1;
  min-width: 120px;
  padding: 10px;
  background: #0EA5E9;
  border: none;
  border-radius: 8px;
  color: #fff;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.3s;
  text-align: center;
}

.use-template-btn:hover {
  opacity: 0.9;
}

.use-template-btn.secondary {
  background: #f8fafc;
  color: #1e293b;
  border: 1px solid #e5e7eb;
}

.use-template-btn.outline {
  background: transparent;
  color: #2563eb;
  border: 1px dashed #93c5fd;
}

.use-template-btn.secondary:hover {
  background: #e2e8f0;
}

.use-template-btn.outline:hover {
  background: rgba(37, 99, 235, 0.08);
}

.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.modal-content {
  background: white;
  border-radius: 20px;
  width: 90%;
  max-width: 800px;
  max-height: 90vh;
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.modal-header {
  padding: 25px 30px;
  border-bottom: 1px solid #e5e7eb;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.modal-header h2 {
  font-size: 1.5rem;
  color: #1e293b;
}

.modal-close {
  background: none;
  border: none;
  font-size: 2rem;
  cursor: pointer;
  color: #94a3b8;
  transition: color 0.3s;
}

.modal-close:hover {
  color: #ef4444;
}

.modal-body {
  flex: 1;
  padding: 30px;
  overflow-y: auto;
}

.generate-form {
  display: flex;
  flex-direction: column;
  gap: 25px;
}

.generate-section {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.generate-hero {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 20px;
  padding: 24px 28px;
  border-radius: 20px;
  background: linear-gradient(120deg, rgba(30, 41, 59, 0.9), rgba(14, 165, 233, 0.9));
  color: #f8fafc;
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.2);
  animation: fadeUp 0.6s ease;
}

.generate-hero h2 {
  font-size: 1.8rem;
  margin-bottom: 8px;
}

.generate-hero-tags {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.generate-hero-tags span {
  padding: 6px 14px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.15);
  border: 1px solid rgba(255, 255, 255, 0.2);
  font-size: 0.8rem;
  letter-spacing: 0.04em;
}

.generate-layout {
  display: flex;
  flex-direction: column;
  gap: 18px;
}

.generate-pagination {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.pagination-tab {
  flex: 1;
  min-width: 140px;
  padding: 10px 16px;
  border-radius: 14px;
  border: 1px solid rgba(148, 163, 184, 0.35);
  background: rgba(255, 255, 255, 0.8);
  font-weight: 600;
  color: #475569;
  display: inline-flex;
  align-items: center;
  gap: 10px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.pagination-tab .tab-index {
  width: 30px;
  height: 30px;
  border-radius: 10px;
  background: rgba(99, 102, 241, 0.15);
  color: #4338ca;
  display: grid;
  place-items: center;
  font-weight: 700;
  font-size: 0.8rem;
}

.pagination-tab.active {
  color: #1e1b4b;
  border-color: rgba(99, 102, 241, 0.6);
  background: linear-gradient(135deg, rgba(224, 231, 255, 0.8), rgba(255, 255, 255, 0.95));
  box-shadow: 0 12px 26px rgba(99, 102, 241, 0.2);
}

.generate-nav {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  padding: 6px 0 0;
}

.generate-preview-panel,
.generate-workbench,
.outline-panel {
  background: rgba(255, 255, 255, 0.9);
  border-radius: 20px;
  padding: 22px;
  border: 1px solid rgba(148, 163, 184, 0.25);
  box-shadow: 0 18px 40px rgba(15, 23, 42, 0.08);
  backdrop-filter: blur(8px);
}

.panel-title {
  display: flex;
  flex-direction: column;
  gap: 10px;
  margin-bottom: 18px;
}

.panel-title h3 {
  margin-bottom: 6px;
}

.generate-workbench {
  display: flex;
  flex-direction: column;
  gap: 18px;
}

.outline-panel {
  display: flex;
  flex-direction: column;
  gap: 16px;
  background: linear-gradient(135deg, rgba(224, 231, 255, 0.6), rgba(255, 255, 255, 0.95));
}

.step-card {
  background: var(--color-bg-card);
  border-radius: var(--radius-card);
  padding: var(--space-lg);
  border: 1px solid var(--color-border);
  box-shadow: var(--shadow-card);
  animation: fadeUp 0.6s ease;
}

.step-header {
  display: flex;
  gap: 16px;
  align-items: center;
  margin-bottom: 14px;
}

.step-index {
  width: 38px;
  height: 38px;
  border-radius: var(--radius-btn);
  display: grid;
  place-items: center;
  font-weight: 700;
  color: var(--color-primary-hover);
  background: var(--color-bg-page-alt);
  font-size: var(--text-caption);
  text-transform: uppercase;
}

.step-body {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.inline-group {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

.action-bar {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  padding: 10px 0 0;
}

.action-primary,
.action-secondary {
  padding: var(--space-sm) var(--space-lg);
  border-radius: var(--radius-btn);
  font-weight: 600;
  cursor: pointer;
  border: none;
  transition: transform var(--transition-default), box-shadow var(--transition-default);
}

.action-primary {
  background: linear-gradient(120deg, #0EA5E9, #38BDF8);
  color: #ffffff;
  box-shadow: 0 12px 24px rgba(14, 165, 233, 0.3);
}

.action-secondary {
  background: #eef2ff;
  color: #3730a3;
}

.action-primary:hover:not(:disabled),
.action-secondary:hover:not(:disabled) {
  transform: translateY(-1px);
}

.action-primary:disabled,
.action-secondary:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

@keyframes fadeUp {
  from {
    opacity: 0;
    transform: translateY(12px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

/* ===== 大纲面板重构样式 ===== */
.outline-panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
  margin-bottom: 16px;
}

.outline-panel-title {
  display: flex;
  align-items: center;
  gap: 12px;
}

.outline-panel-title h3 {
  margin: 0 0 2px;
  font-size: 1rem;
  font-weight: 700;
  color: #1e293b;
}

.outline-panel-title p {
  margin: 0;
  font-size: 0.82rem;
  color: #64748b;
}

.outline-toolbar {
  display: flex;
  gap: 8px;
  align-items: center;
  flex-shrink: 0;
}

.outline-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 7px 16px;
  border-radius: 8px;
  border: 1px solid #cbd5e1;
  background: #ffffff;
  color: #334155;
  font-size: 0.875rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.18s ease;
  white-space: nowrap;
}

.outline-btn:hover:not(:disabled) {
  border-color: #94a3b8;
  background: #f8fafc;
}

.outline-btn.primary {
  background: linear-gradient(120deg, #0EA5E9, #38BDF8);
  color: #fff;
  border-color: transparent;
  box-shadow: 0 4px 12px rgba(14, 165, 233, 0.28);
}

.outline-btn.primary:hover:not(:disabled) {
  box-shadow: 0 6px 16px rgba(14, 165, 233, 0.38);
  transform: translateY(-1px);
}

.outline-btn:disabled {
  opacity: 0.55;
  cursor: not-allowed;
}

/* 加载旋转动画 */
.outline-spinner {
  display: inline-block;
  width: 14px;
  height: 14px;
  border: 2px solid rgba(255,255,255,0.4);
  border-top-color: #fff;
  border-radius: 50%;
  animation: spin 0.7s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

/* 骨架屏 */
.outline-skeleton {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.outline-skeleton-item {
  border-radius: 12px;
  padding: 16px;
  background: #f1f5f9;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.skeleton-line {
  height: 12px;
  border-radius: 6px;
  background: linear-gradient(90deg, #e2e8f0 25%, #f1f5f9 50%, #e2e8f0 75%);
  background-size: 200% 100%;
  animation: shimmer 1.4s infinite;
  width: 100%;
}

.skeleton-line.short { width: 30%; }
.skeleton-line.medium { width: 60%; }

@keyframes shimmer {
  0% { background-position: 200% 0; }
  100% { background-position: -200% 0; }
}

/* 大纲列表 */
.outline-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

/* 大纲卡片 */
.outline-item {
  border-radius: 12px;
  border: 1px solid #e2e8f0;
  border-left: 4px solid #38BDF8;
  background: #fff;
  overflow: hidden;
  transition: box-shadow 0.2s ease, border-color 0.2s ease;
}

.outline-item:hover {
  box-shadow: 0 4px 16px rgba(14, 165, 233, 0.1);
}

.outline-item.is-expanded {
  border-color: #7dd3fc;
  box-shadow: 0 6px 20px rgba(14, 165, 233, 0.14);
}

/* 不同页面类型左边框颜色 */
.outline-type-cover  { border-left-color: #a78bfa; }
.outline-type-toc    { border-left-color: #34d399; }
.outline-type-content{ border-left-color: #38BDF8; }
.outline-type-summary{ border-left-color: #fb923c; }

/* 卡片头部 */
.outline-item-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 14px;
  cursor: pointer;
  user-select: none;
  gap: 8px;
  background: #fafbfc;
  transition: background 0.15s;
}

.outline-item-header:hover {
  background: #f1f5f9;
}

.outline-item-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
  flex: 1;
}

.outline-page-badge {
  flex-shrink: 0;
  width: 28px;
  height: 28px;
  border-radius: 50%;
  background: #e0f2fe;
  color: #0369a1;
  font-size: 0.75rem;
  font-weight: 700;
  display: grid;
  place-items: center;
}

.outline-type-tag {
  flex-shrink: 0;
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 0.72rem;
  font-weight: 600;
}

.tag-cover   { background: #ede9fe; color: #6d28d9; }
.tag-toc     { background: #d1fae5; color: #065f46; }
.tag-content { background: #e0f2fe; color: #0369a1; }
.tag-summary { background: #ffedd5; color: #9a3412; }

.outline-item-title-preview {
  font-size: 0.88rem;
  font-weight: 600;
  color: #1e293b;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  min-width: 0;
}

.outline-item-actions {
  display: flex;
  align-items: center;
  gap: 4px;
  flex-shrink: 0;
}

.outline-icon-btn {
  width: 26px;
  height: 26px;
  border-radius: 6px;
  border: 1px solid #e2e8f0;
  background: #fff;
  color: #64748b;
  font-size: 0.8rem;
  cursor: pointer;
  display: grid;
  place-items: center;
  transition: all 0.15s;
  padding: 0;
}

.outline-icon-btn:hover:not(:disabled) {
  background: #f1f5f9;
  color: #334155;
  border-color: #cbd5e1;
}

.outline-icon-btn:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}

.outline-icon-btn.danger:hover:not(:disabled) {
  background: #fef2f2;
  color: #ef4444;
  border-color: #fecaca;
}

.outline-expand-arrow {
  font-size: 0.7rem;
  color: #94a3b8;
  margin-left: 4px;
}

/* 展开编辑区 */
.outline-item-body {
  padding: 14px 16px;
  display: flex;
  flex-direction: column;
  gap: 14px;
  border-top: 1px solid #f1f5f9;
  background: #fff;
}

.outline-field {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.outline-field-label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 0.8rem;
  font-weight: 600;
  color: #475569;
}

.outline-hint {
  font-weight: 400;
  color: #94a3b8;
  font-size: 0.75rem;
}

.outline-char-count {
  margin-left: auto;
  font-weight: 400;
  color: #94a3b8;
  font-size: 0.75rem;
}

.outline-char-count.warn {
  color: #f59e0b;
}

/* 页面类型选择器 */
.outline-type-selector {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.type-option {
  padding: 4px 12px;
  border-radius: 999px;
  border: 1px solid #e2e8f0;
  background: #f8fafc;
  color: #64748b;
  font-size: 0.8rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
}

.type-option:hover {
  border-color: #94a3b8;
  color: #334155;
}

.type-option.active {
  background: #0EA5E9;
  color: #fff;
  border-color: #0EA5E9;
}

/* 输入框统一样式 */
.outline-input {
  width: 100%;
  padding: 8px 12px;
  border-radius: 8px;
  border: 1px solid #e2e8f0;
  background: #f8fafc;
  font-size: 0.875rem;
  color: #1e293b;
  transition: border-color 0.18s, box-shadow 0.18s, background 0.18s;
  box-sizing: border-box;
}

.outline-input:focus {
  outline: none;
  border-color: #7dd3fc;
  background: #fff;
  box-shadow: 0 0 0 3px rgba(14, 165, 233, 0.1);
}

/* 要点列表 */
.outline-keypoints {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.outline-keypoint-row {
  display: flex;
  align-items: center;
  gap: 6px;
}

.kp-dot {
  color: #94a3b8;
  font-size: 1.1rem;
  flex-shrink: 0;
  width: 14px;
  text-align: center;
}

.kp-input {
  flex: 1;
}

.kp-remove-btn {
  flex-shrink: 0;
  width: 22px;
  height: 22px;
  border-radius: 50%;
  border: none;
  background: transparent;
  color: #cbd5e1;
  font-size: 0.75rem;
  cursor: pointer;
  display: grid;
  place-items: center;
  transition: all 0.15s;
  padding: 0;
}

.kp-remove-btn:hover {
  background: #fef2f2;
  color: #ef4444;
}

.kp-add-btn {
  align-self: flex-start;
  padding: 4px 10px;
  border-radius: 6px;
  border: 1px dashed #cbd5e1;
  background: transparent;
  color: #64748b;
  font-size: 0.8rem;
  cursor: pointer;
  transition: all 0.15s;
  margin-top: 2px;
}

.kp-add-btn:hover {
  border-color: #0EA5E9;
  color: #0EA5E9;
  background: #f0f9ff;
}

/* 末尾新增卡片按钮 */
.outline-add-card {
  width: 100%;
  padding: 12px;
  border-radius: 12px;
  border: 2px dashed #e2e8f0;
  background: transparent;
  color: #94a3b8;
  font-size: 0.875rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.18s;
  text-align: center;
}

.outline-add-card:hover {
  border-color: #7dd3fc;
  color: #0EA5E9;
  background: #f0f9ff;
}

/* 空状态 */
.outline-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  padding: 40px 20px;
  color: #94a3b8;
  text-align: center;
}

.outline-empty-icon {
  font-size: 2.4rem;
  line-height: 1;
}

.outline-empty p {
  font-size: 0.875rem;
  max-width: 280px;
  line-height: 1.6;
}

/* 底部操作栏 */
.outline-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 0 0;
  border-top: 1px solid #f1f5f9;
  margin-top: 4px;
  gap: 12px;
  flex-wrap: wrap;
}

.outline-footer-tip {
  font-size: 0.82rem;
  color: #94a3b8;
}

/* 旧的 outline-generate-bar 兼容 */
.outline-generate-bar {
  padding-top: 14px;
  border-top: 1px solid #f1f5f9;
}

.form-group label {
  display: block;
  margin-bottom: 8px;
  font-weight: 500;
  color: #1e293b;
}

.form-group input,
.form-group textarea {
  width: 100%;
  padding: 12px 16px;
  border: 2px solid #e5e7eb;
  border-radius: 10px;
  font-size: 1rem;
  transition: border-color 0.3s;
}

.form-group input:focus,
.form-group textarea:focus {
  outline: none;
  border-color: #0EA5E9;
}

.form-group-inline {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}
.form-group-inline label { margin-bottom: 0; }
.mt-1 { margin-top: 8px; }
.select-sm, .select-md {
  padding: 8px 12px;
  border: 2px solid #e5e7eb;
  border-radius: 8px;
  font-size: 0.95rem;
}
.select-sm { max-width: 80px; }
.select-md { min-width: 180px; max-width: 240px; }

.style-options {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.style-option {
  padding: 10px 20px;
  border: 2px solid #e5e7eb;
  border-radius: 10px;
  cursor: pointer;
  transition: all 0.3s;
}

.style-option:hover {
  border-color: #0EA5E9;
}

.style-option.selected {
  background: #e0e7ff;
  border-color: #0EA5E9;
  color: #0EA5E9;
  font-weight: 500;
}

.template-options {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.template-option {
  display: flex;
  gap: 16px;
  padding: 12px;
  border: 2px solid #e2e8f0;
  border-radius: 14px;
  cursor: pointer;
  transition: border-color 0.2s, background 0.2s;
}

.template-option.selected {
  border-color: #0EA5E9;
  background: #f5f3ff;
}

.template-thumb {
  width: 140px;
  height: 90px;
  border-radius: 10px;
  overflow: hidden;
  background: #e2e8f0;
  display: flex;
  align-items: center;
  justify-content: center;
}

.template-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.template-thumb-fallback {
  font-size: 0.85rem;
  color: #475569;
}

.template-option-info strong {
  display: block;
  font-size: 1rem;
  color: #0f172a;
}

.template-option-info small {
  display: block;
  color: #64748b;
  margin-bottom: 8px;
}

.template-option-info p {
  margin-bottom: 8px;
  color: #475569;
  font-size: 0.9rem;
}

.template-option-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.template-option-tags span {
  background: #eef2ff;
  color: #4338ca;
  padding: 2px 8px;
  border-radius: 999px;
  font-size: 0.75rem;
}

.generate-options {
  display: flex;
  gap: 20px;
}

.option-checkbox {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
}

.model-options {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.model-option {
  border: 2px solid #e2e8f0;
  border-radius: 12px;
  padding: 12px 16px;
  cursor: pointer;
  transition: border-color 0.2s, background 0.2s;
}

.model-option.selected {
  border-color: #0EA5E9;
  background: #f5f3ff;
}

.model-title {
  font-weight: 600;
  color: #111827;
}

.model-desc {
  margin: 6px 0;
  color: #475569;
  font-size: 0.9rem;
}

.modal-footer {
  padding: 20px 30px;
  border-top: 1px solid #e5e7eb;
  display: flex;
  justify-content: flex-end;
  gap: 15px;
}

.modal-btn {
  padding: 12px 24px;
  border: none;
  border-radius: 10px;
  font-size: 1rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.3s;
}

.modal-btn.secondary {
  background: #f1f5f9;
  color: #64748b;
}

.modal-btn.secondary:hover {
  background: #e2e8f0;
}

.modal-btn.primary {
  background: linear-gradient(135deg, #0EA5E9 0%, #38BDF8 100%);
  color: white;
}

.modal-btn.primary:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 10px 20px rgba(14, 165, 233, 0.3);
}

.modal-btn.primary:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

@media (max-width: 1200px) {
  .stats-grid,
  .features-grid,
  .templates-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 992px) {
  .sidebar {
    width: 80px;
  }

  .sidebar-header h2,
  .nav-text,
  .user-details,
  .logout-btn span:last-child {
    display: none;
  }

  .avatar {
    width: 40px;
    height: 40px;
    font-size: 1rem;
  }

  .nav-item {
    justify-content: center;
    padding: 15px;
  }

  .logout-btn {
    justify-content: center;
  }
}

@media (max-width: 768px) {
  .stats-grid,
  .features-grid,
  .templates-grid {
    grid-template-columns: 1fr;
  }

  .generate-hero {
    flex-direction: column;
    align-items: flex-start;
  }

  .inline-group {
    grid-template-columns: 1fr;
  }

  .generate-options {
    flex-direction: column;
    align-items: flex-start;
  }

  .main-container {
    flex-direction: column;
  }

  .sidebar {
    width: 100%;
    height: 60px;
    flex-direction: row;
    justify-content: space-between;
    padding: 0 20px;
  }

  .sidebar-header,
  .sidebar-footer {
    display: none;
  }

  .sidebar-nav {
    display: flex;
    padding: 0;
  }

  .nav-item {
    flex: 1;
    flex-direction: column;
    padding: 10px;
  }

  .main-header {
    flex-direction: column;
    gap: 15px;
    text-align: center;
  }

}

/* ===== 来源选择 / 文档上传 ===== */
.source-card {
  border-left: 4px solid #0EA5E9;
}

.source-tabs {
  display: flex;
  gap: 12px;
  margin-bottom: 20px;
}

.source-tab {
  padding: 8px 22px;
  border: 2px solid #e2e8f0;
  border-radius: 999px;
  background: transparent;
  color: #475569;
  font-size: 0.95rem;
  cursor: pointer;
  transition: all 0.2s;
}

.source-tab.active {
  border-color: #0EA5E9;
  background: #e0f2fe;
  color: #0369a1;
  font-weight: 600;
}

.material-upload-area {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.upload-zone {
  border: 2px dashed #cbd5e1;
  border-radius: 12px;
  padding: 32px;
  text-align: center;
  cursor: pointer;
  transition: border-color 0.2s, background 0.2s;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  color: #64748b;
}

.upload-zone:hover {
  border-color: #0EA5E9;
  background: #f0f9ff;
}

.upload-filename {
  font-weight: 600;
  color: #0f172a;
}

.upload-actions {
  display: flex;
  gap: 12px;
}

.extract-status {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 12px 16px;
  background: #f8fafc;
  border-radius: 10px;
}

.extract-spinner {
  display: flex;
  align-items: center;
  gap: 8px;
  color: #64748b;
  font-size: 0.9rem;
}

.spinner-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #0EA5E9;
  animation: pulse 1.2s infinite;
  display: inline-block;
}

@keyframes pulse {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.4; transform: scale(0.7); }
}

.extract-result-card {
  background: #f8fafc;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.extract-result-card h4 {
  margin: 0 0 4px;
  color: #0f172a;
  font-size: 1rem;
}

.extract-field {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.field-label {
  font-size: 0.78rem;
  font-weight: 700;
  color: #0EA5E9;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.extract-list {
  margin: 4px 0 0 16px;
  padding: 0;
  color: #334155;
  font-size: 0.9rem;
  line-height: 1.7;
}

.keyword-tags {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 4px;
}

.extract-actions {
  margin-top: 8px;
}

.material-history-hint {
  font-size: 0.88rem;
  color: #64748b;
  display: flex;
  align-items: center;
  gap: 6px;
}

.link-btn {
  background: none;
  border: none;
  color: #0EA5E9;
  cursor: pointer;
  font-size: inherit;
  padding: 0;
  text-decoration: underline;
}

.material-bound-hint {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 12px;
  background: #f0fdf4;
  border-radius: 8px;
  font-size: 0.9rem;
}

/* ===== 材料管理页 ===== */
.materials-section {
  padding: 0;
}

.materials-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  margin-bottom: 28px;
  flex-wrap: wrap;
  gap: 16px;
}

.materials-header h2 {
  margin: 0 0 6px;
  font-size: 1.6rem;
  color: #0f172a;
}

.materials-header p {
  margin: 0;
  color: #64748b;
  font-size: 0.95rem;
}

.materials-loading,
.materials-empty {
  text-align: center;
  padding: 60px 20px;
  color: #94a3b8;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
}

.materials-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  gap: 20px;
}

.material-card {
  background: white;
  border: 1px solid #e2e8f0;
  border-radius: 14px;
  padding: 18px 20px;
  display: flex;
  flex-direction: column;
  gap: 14px;
  box-shadow: 0 2px 8px rgba(0,0,0,0.04);
  transition: box-shadow 0.2s;
}

.material-card:hover {
  box-shadow: 0 6px 20px rgba(14,165,233,0.12);
  border-color: #bae6fd;
}

.material-card-header {
  display: flex;
  gap: 14px;
  align-items: flex-start;
}

.material-icon {
  width: 42px;
  height: 42px;
  background: #e0f2fe;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #0EA5E9;
  flex-shrink: 0;
}

.material-info {
  flex: 1;
  min-width: 0;
}

.material-filename {
  font-weight: 600;
  color: #0f172a;
  font-size: 0.95rem;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.material-meta {
  display: flex;
  gap: 10px;
  margin-top: 4px;
  font-size: 0.8rem;
  color: #94a3b8;
  flex-wrap: wrap;
}

.material-type {
  background: #f1f5f9;
  color: #475569;
  padding: 1px 6px;
  border-radius: 4px;
  font-weight: 600;
  font-size: 0.75rem;
}

.material-card-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 8px;
}

.material-actions {
  display: flex;
  gap: 8px;
}

.mat-btn {
  padding: 6px 14px;
  border-radius: 8px;
  border: none;
  font-size: 0.85rem;
  cursor: pointer;
  font-weight: 500;
  transition: all 0.2s;
}

.mat-btn.primary {
  background: #0EA5E9;
  color: white;
}

.mat-btn.primary:hover {
  background: #0284c7;
}

.mat-btn.danger {
  background: #fee2e2;
  color: #dc2626;
}

.mat-btn.danger:hover {
  background: #fecaca;
}

/* ===== 材料详情弹窗 ===== */
.material-modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.45);
  z-index: 1000;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px;
}

.material-modal {
  background: white;
  border-radius: 16px;
  width: 100%;
  max-width: 600px;
  max-height: 85vh;
  display: flex;
  flex-direction: column;
  box-shadow: 0 20px 60px rgba(0,0,0,0.2);
}

.material-modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 20px 24px;
  border-bottom: 1px solid #e5e7eb;
}

.material-modal-header h3 {
  margin: 0;
  font-size: 1.1rem;
  color: #0f172a;
}

.modal-close {
  background: none;
  border: none;
  font-size: 1.1rem;
  cursor: pointer;
  color: #94a3b8;
  padding: 4px 8px;
  border-radius: 6px;
  transition: background 0.2s;
}

.modal-close:hover {
  background: #f1f5f9;
  color: #475569;
}

.material-modal-body {
  flex: 1;
  overflow-y: auto;
  padding: 20px 24px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.material-modal-loading {
  padding: 40px;
  text-align: center;
  color: #94a3b8;
}

.material-modal-footer {
  padding: 16px 24px;
  border-top: 1px solid #e5e7eb;
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}
</style>
