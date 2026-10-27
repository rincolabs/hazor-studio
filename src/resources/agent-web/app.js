(() => {
    'use strict';

    const messages = document.querySelector('#messages');
    const emptyState = document.querySelector('#empty-state');
    const prompt = document.querySelector('#prompt');
    const sendButton = document.querySelector('#send-button');
    const settingsButton = document.querySelector('#settings-button');
    const agentSelect = document.querySelector('#agent-select');
    const modelInfo = document.querySelector('#model-info');
    const status = document.querySelector('#status');

    let bridge = null;
    let processing = false;
    let streamingMessage = null;
    let streamingText = '';

    const escapeHtml = (value) => String(value)
        .replaceAll('&', '&amp;')
        .replaceAll('<', '&lt;')
        .replaceAll('>', '&gt;')
        .replaceAll('"', '&quot;')
        .replaceAll("'", '&#039;');

    const inlineMarkdown = (value) => {
        let text = escapeHtml(value);
        text = text.replace(/`([^`]+)`/g, '<code>$1</code>');
        text = text.replace(/\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g,
            '<a href="$2" data-external-link="$2">$1</a>');
        text = text.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
        text = text.replace(/(^|\s)\*([^*\n]+)\*/g, '$1<em>$2</em>');
        return text;
    };

    const highlightCode = (code, language) => {
        let highlighted = escapeHtml(code);
        const supported = /^(js|javascript|ts|typescript|cpp|c\+\+|c|python|py|json|bash|sh|cmake)$/i;
        if (!supported.test(language || ''))
            return highlighted;
        highlighted = highlighted.replace(
            /\b(class|const|let|var|function|return|if|else|for|while|switch|case|break|new|true|false|null|void|int|float|double|bool|auto|public|private|protected|namespace|include|def|import|from|as|try|catch|throw|async|await)\b/g,
            '<span class="tok-keyword">$1</span>');
        highlighted = highlighted.replace(/\b(\d+(?:\.\d+)?)\b/g,
            '<span class="tok-number">$1</span>');
        return highlighted;
    };

    const splitTableRow = (line) => line.trim().replace(/^\||\|$/g, '')
        .split('|').map((cell) => cell.trim());

    const renderMarkdown = (source) => {
        const lines = String(source).replace(/\r\n?/g, '\n').split('\n');
        const output = [];
        let index = 0;

        while (index < lines.length) {
            const line = lines[index];
            const fence = line.match(/^```\s*([\w+.-]*)\s*$/);
            if (fence) {
                const language = fence[1] || 'code';
                const code = [];
                index += 1;
                while (index < lines.length && !/^```\s*$/.test(lines[index])) {
                    code.push(lines[index]);
                    index += 1;
                }
                if (index < lines.length)
                    index += 1;
                output.push(`<div class="code-block"><div class="code-header"><span>${escapeHtml(language)}</span><button class="copy-code" type="button">Copy</button></div><pre><code>${highlightCode(code.join('\n'), language)}</code></pre></div>`);
                continue;
            }

            if (line.includes('|') && index + 1 < lines.length &&
                /^\s*\|?\s*:?-{3,}/.test(lines[index + 1])) {
                const headers = splitTableRow(line);
                index += 2;
                const rows = [];
                while (index < lines.length && lines[index].includes('|') && lines[index].trim()) {
                    rows.push(splitTableRow(lines[index]));
                    index += 1;
                }
                output.push(`<table><thead><tr>${headers.map((cell) => `<th>${inlineMarkdown(cell)}</th>`).join('')}</tr></thead><tbody>${rows.map((row) => `<tr>${row.map((cell) => `<td>${inlineMarkdown(cell)}</td>`).join('')}</tr>`).join('')}</tbody></table>`);
                continue;
            }

            const heading = line.match(/^(#{1,4})\s+(.+)$/);
            if (heading) {
                const level = Math.min(heading[1].length + 1, 5);
                output.push(`<h${level}>${inlineMarkdown(heading[2])}</h${level}>`);
                index += 1;
                continue;
            }

            if (/^\s*[-*]\s+/.test(line)) {
                const items = [];
                while (index < lines.length && /^\s*[-*]\s+/.test(lines[index])) {
                    items.push(lines[index].replace(/^\s*[-*]\s+/, ''));
                    index += 1;
                }
                output.push(`<ul>${items.map((item) => `<li>${inlineMarkdown(item)}</li>`).join('')}</ul>`);
                continue;
            }

            if (/^\s*\d+\.\s+/.test(line)) {
                const items = [];
                while (index < lines.length && /^\s*\d+\.\s+/.test(lines[index])) {
                    items.push(lines[index].replace(/^\s*\d+\.\s+/, ''));
                    index += 1;
                }
                output.push(`<ol>${items.map((item) => `<li>${inlineMarkdown(item)}</li>`).join('')}</ol>`);
                continue;
            }

            if (/^>\s?/.test(line)) {
                const quote = [];
                while (index < lines.length && /^>\s?/.test(lines[index])) {
                    quote.push(lines[index].replace(/^>\s?/, ''));
                    index += 1;
                }
                output.push(`<blockquote>${inlineMarkdown(quote.join('\n')).replaceAll('\n', '<br>')}</blockquote>`);
                continue;
            }

            if (!line.trim()) {
                index += 1;
                continue;
            }

            const paragraph = [line];
            index += 1;
            while (index < lines.length && lines[index].trim() &&
                   !/^```/.test(lines[index]) &&
                   !/^(#{1,4})\s+/.test(lines[index]) &&
                   !/^\s*(?:[-*]|\d+\.)\s+/.test(lines[index]) &&
                   !/^>\s?/.test(lines[index])) {
                paragraph.push(lines[index]);
                index += 1;
            }
            output.push(`<p>${inlineMarkdown(paragraph.join('\n')).replaceAll('\n', '<br>')}</p>`);
        }

        return output.join('');
    };

    const nearBottom = () => messages.scrollHeight - messages.scrollTop - messages.clientHeight < 84;
    const followIfNeeded = (follow) => {
        if (follow)
            messages.scrollTop = messages.scrollHeight;
    };

    const messageElement = (role, timestamp, extraClass = '') => {
        const follow = nearBottom();
        emptyState.classList.add('hidden');
        const article = document.createElement('article');
        article.className = `message ${role} ${extraClass}`.trim();
        article.innerHTML = '<div class="message-body"><div class="message-content"></div></div>' +
            `<div class="message-meta">${escapeHtml(timestamp)}</div>`;
        messages.append(article);
        followIfNeeded(follow);
        return article;
    };

    const addMarkdownMessage = (role, text, timestamp, extraClass = '') => {
        const article = messageElement(role, timestamp, extraClass);
        article.querySelector('.message-content').innerHTML = renderMarkdown(text);
        messages.dispatchEvent(new Event('content-added'));
        return article;
    };

    const setProcessing = (value) => {
        processing = Boolean(value);
        prompt.disabled = processing;
        status.textContent = processing ? 'Processing…' : 'Ready';
        status.classList.toggle('processing', processing);
        updateComposer();
        if (!processing)
            prompt.focus();
    };

    const updateComposer = () => {
        prompt.style.height = 'auto';
        prompt.style.height = `${Math.min(prompt.scrollHeight, 120)}px`;
        sendButton.disabled = processing || !prompt.value.trim() || !bridge;
    };

    const submit = () => {
        if (!bridge || processing)
            return;
        const text = prompt.value.trim();
        if (!text)
            return;
        prompt.value = '';
        updateComposer();
        bridge.sendMessage(text);
    };

    const updateAgents = (names, activeName) => {
        const previous = activeName || agentSelect.value;
        agentSelect.replaceChildren();
        (names || []).forEach((name) => {
            const option = document.createElement('option');
            option.value = name;
            option.textContent = name;
            option.selected = name === previous;
            agentSelect.append(option);
        });
        agentSelect.disabled = agentSelect.options.length === 0;
    };

    const applyTheme = (theme) => {
        if (!theme)
            return;
        const variables = {
            background: 'background', surface: 'surface', surfaceDark: 'surface-dark',
            surfaceHover: 'surface-hover', surfaceSelected: 'surface-selected',
            text: 'text', textBright: 'text-bright', textSecondary: 'text-secondary',
            textDisabled: 'text-disabled', border: 'border', borderFocus: 'border-focus',
            accent: 'accent', accentHover: 'accent-hover', danger: 'danger',
            success: 'success', warning: 'warning'
        };
        Object.entries(variables).forEach(([key, cssName]) => {
            if (theme[key])
                document.documentElement.style.setProperty(`--${cssName}`, theme[key]);
        });
        if (theme.fontFamily)
            document.documentElement.style.setProperty('--font-family', `"${theme.fontFamily}", sans-serif`);
        if (theme.fontFamilyMono)
            document.documentElement.style.setProperty('--font-mono', `"${theme.fontFamilyMono}", monospace`);
        if (theme.fontSize)
            document.documentElement.style.setProperty('--font-size', `${theme.fontSize}px`);
        if (theme.radius)
            document.documentElement.style.setProperty('--radius', `${theme.radius}px`);
    };

    const connectBridge = (object) => {
        bridge = object;

        bridge.initialState((state) => {
            updateAgents(state.agents, state.activeAgent);
            modelInfo.textContent = state.modelInfo || 'Model';
            applyTheme(state.theme);
            setProcessing(state.processing);
            updateComposer();
            prompt.focus();
        });

        bridge.userMessageAdded.connect((text, timestamp) => {
            addMarkdownMessage('user', text, timestamp);
        });
        bridge.assistantMessageStarted.connect((timestamp) => {
            streamingText = '';
            streamingMessage = messageElement('assistant', timestamp);
            streamingMessage.querySelector('.message-content').innerHTML =
                '<span class="typing" aria-label="Generating response"><span></span><span></span><span></span></span>';
        });
        bridge.assistantMessageChunk.connect((chunk) => {
            if (!streamingMessage)
                streamingMessage = messageElement('assistant', '');
            const follow = nearBottom();
            streamingText += chunk;
            streamingMessage.querySelector('.message-content').innerHTML = renderMarkdown(streamingText);
            followIfNeeded(follow);
        });
        bridge.assistantMessageFinished.connect(() => {
            streamingMessage = null;
            streamingText = '';
        });
        bridge.statusMessageAdded.connect((text, timestamp) => {
            addMarkdownMessage('system', text, timestamp);
        });
        bridge.toolMessageAdded.connect((name, success, timestamp) => {
            addMarkdownMessage('tool', `${success ? '✓' : '✗'} ${name}`, timestamp,
                success ? 'success' : 'failure');
        });
        bridge.errorAdded.connect((text, timestamp) => {
            addMarkdownMessage('error', text, timestamp);
        });
        bridge.processingChanged.connect(setProcessing);
        bridge.agentStateChanged.connect(updateAgents);
        bridge.modelInfoChanged.connect((info) => {
            modelInfo.textContent = info || 'Model';
        });
        bridge.themeChanged.connect(applyTheme);

        updateComposer();
    };

    prompt.addEventListener('input', updateComposer);
    prompt.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' && !event.shiftKey && !event.isComposing) {
            event.preventDefault();
            submit();
        }
    });
    sendButton.addEventListener('click', submit);
    settingsButton.addEventListener('click', () => bridge?.requestConfiguration());
    agentSelect.addEventListener('change', () => bridge?.selectAgent(agentSelect.value));

    messages.addEventListener('click', async (event) => {
        const copy = event.target.closest('.copy-code');
        if (copy) {
            const code = copy.closest('.code-block').querySelector('code').textContent;
            try {
                await navigator.clipboard.writeText(code);
                copy.textContent = 'Copied';
            } catch (_) {
                const fallback = document.createElement('textarea');
                fallback.value = code;
                document.body.append(fallback);
                fallback.select();
                document.execCommand('copy');
                fallback.remove();
                copy.textContent = 'Copied';
            }
            window.setTimeout(() => { copy.textContent = 'Copy'; }, 1200);
            return;
        }

        const link = event.target.closest('[data-external-link]');
        if (link) {
            event.preventDefault();
            bridge?.openExternalLink(link.dataset.externalLink);
        }
    });

    updateComposer();
    new QWebChannel(qt.webChannelTransport, (channel) => {
        connectBridge(channel.objects.agentBridge);
    });
})();
