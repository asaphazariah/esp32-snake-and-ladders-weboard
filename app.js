// c:\Users\paulg\.gemini\antigravity\scratch\esp32-jumbotron\app.js

const SNAKES = {
  25: 3,
  42: 1,
  56: 48,
  61: 43,
  92: 67,
  94: 12,
  98: 80
};

const LADDERS = {
  7: 30,
  16: 33,
  20: 38,
  36: 83,
  50: 68,
  63: 81,
  71: 89,
  86: 97
};

const PLAYER_COLORS = [
  { name: 'Red',    color: '#e74c3c', var: '--player-1' },
  { name: 'Blue',   color: '#3498db', var: '--player-2' },
  { name: 'Green',  color: '#2ecc71', var: '--player-3' },
  { name: 'Yellow', color: '#f1c40f', var: '--player-4' },
  { name: 'Purple', color: '#9b59b6', var: '--player-5' },
  { name: 'Orange', color: '#e67e22', var: '--player-6' },
  { name: 'Pink',   color: '#e91e90', var: '--player-7' },
  { name: 'White',  color: '#ecf0f1', var: '--player-8' },
];

class SoundManager {
    constructor() {
        this.enabled = true;
        this.audioCtx = null;
    }
    
    initAudio() {
        if (!this.audioCtx) {
            this.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        }
    }

    toggle() {
        this.enabled = !this.enabled;
    }

    playOscillator(freq, type, duration, vol=0.1, sweep=false, sweepEndFreq=0) {
        if (!this.enabled) return;
        this.initAudio();
        
        const osc = this.audioCtx.createOscillator();
        const gainNode = this.audioCtx.createGain();
        
        osc.type = type;
        osc.frequency.setValueAtTime(freq, this.audioCtx.currentTime);
        if (sweep) {
            osc.frequency.exponentialRampToValueAtTime(sweepEndFreq, this.audioCtx.currentTime + duration);
        }
        
        gainNode.gain.setValueAtTime(vol, this.audioCtx.currentTime);
        gainNode.gain.exponentialRampToValueAtTime(0.01, this.audioCtx.currentTime + duration);
        
        osc.connect(gainNode);
        gainNode.connect(this.audioCtx.destination);
        
        osc.start();
        osc.stop(this.audioCtx.currentTime + duration);
    }

    playDiceRoll() {
        if (!this.enabled) return;
        this.playOscillator(400, 'square', 0.1, 0.05);
        setTimeout(() => this.playOscillator(600, 'square', 0.1, 0.05), 100);
        setTimeout(() => this.playOscillator(300, 'square', 0.1, 0.05), 200);
    }

    playMove() {
        this.playOscillator(800, 'sine', 0.05, 0.1);
    }

    playSnake() {
        this.playOscillator(400, 'sawtooth', 0.8, 0.2, true, 100);
    }

    playLadder() {
        this.playOscillator(300, 'sine', 0.6, 0.2, true, 800);
    }

    playWin() {
        if (!this.enabled) return;
        const notes = [523.25, 659.25, 783.99, 1046.50];
        notes.forEach((freq, i) => {
            setTimeout(() => this.playOscillator(freq, 'square', 0.2, 0.1), i * 150);
        });
        setTimeout(() => this.playOscillator(1046.50, 'square', 0.6, 0.1), notes.length * 150);
    }
}

class BoardRenderer {
    constructor(boardElement) {
        this.boardElement = boardElement;
        this.tokens = new Map();
        this.dots = [
            [], // 0
            [4], // 1
            [2, 6], // 2
            [2, 4, 6], // 3
            [0, 2, 6, 8], // 4
            [0, 2, 4, 6, 8], // 5
            [0, 2, 3, 5, 6, 8] // 6
        ];
    }

    initBoard() {
        this.boardElement.innerHTML = '';
        for (let i = 1; i <= 100; i++) {
            const cell = document.createElement('div');
            cell.className = 'cell';
            cell.dataset.cell = i;
            
            const { row, col } = this.cellToPosition(i);
            // CSS Grid starts from 1, row 1 is top. 
            // row 0 in game is bottom. So row 0 -> grid row 10
            cell.style.gridRowStart = 10 - row;
            cell.style.gridColumnStart = col + 1;
            
            const label = document.createElement('div');
            label.className = 'cell-label';
            label.textContent = i;
            cell.appendChild(label);
            
            if (SNAKES[i]) cell.classList.add('snake-cell');
            if (LADDERS[i]) cell.classList.add('ladder-cell');
            
            this.boardElement.appendChild(cell);
        }
    }

    cellToPosition(cellNumber) {
        if (cellNumber < 1) cellNumber = 1;
        if (cellNumber > 100) cellNumber = 100;
        const row = Math.floor((cellNumber - 1) / 10);
        const colInRow = (cellNumber - 1) % 10;
        const col = (row % 2 === 0) ? colInRow : (9 - colInRow);
        return { row, col };
    }

    cellToPixelPosition(cellNumber) {
        const { row, col } = this.cellToPosition(cellNumber);
        return { 
            left: col * 10, 
            bottom: row * 10 
        };
    }

    createToken(playerId, colorHex) {
        const token = document.createElement('div');
        token.className = 'token';
        token.style.backgroundColor = colorHex;
        token.dataset.player = playerId;
        this.boardElement.appendChild(token);
        this.tokens.set(playerId, token);
        return token;
    }

    async moveToken(playerId, fromCell, toCell, isSlide) {
        const token = this.tokens.get(playerId);
        if (!token) return;

        if (isSlide) {
            token.style.transition = 'left 1s cubic-bezier(0.25, 0.1, 0.25, 1), bottom 1s cubic-bezier(0.25, 0.1, 0.25, 1)';
            const pos = this.cellToPixelPosition(toCell);
            token.style.left = `${pos.left}%`;
            token.style.bottom = `${pos.bottom}%`;
            await new Promise(r => setTimeout(r, 1000));
        } else {
            token.style.transition = 'left 0.2s linear, bottom 0.2s linear';
            const step = fromCell < toCell ? 1 : -1;
            for (let c = fromCell; c !== toCell; c += step) {
                const nextCell = c + step;
                const pos = this.cellToPixelPosition(nextCell);
                token.style.left = `${pos.left}%`;
                token.style.bottom = `${pos.bottom}%`;
                window.app.soundManager.playMove();
                await new Promise(r => setTimeout(r, 200));
            }
        }
        token.style.transition = '';
    }

    updateTokenPositions(players) {
        const cellCounts = {};
        players.forEach(p => {
            cellCounts[p.position] = (cellCounts[p.position] || 0) + 1;
        });
        
        const cellCurrent = {};
        
        players.forEach(p => {
            const token = this.tokens.get(p.id);
            if (!token) return;
            const pos = this.cellToPixelPosition(p.position);
            const total = cellCounts[p.position];
            
            if (total > 1) {
                cellCurrent[p.position] = (cellCurrent[p.position] || 0) + 1;
                const idx = cellCurrent[p.position] - 1;
                const offset = (idx - (total - 1) / 2) * 2; // slight shift
                token.style.left = `calc(${pos.left}% + ${offset}px)`;
                token.style.bottom = `calc(${pos.bottom}% + ${offset}px)`;
            } else {
                token.style.left = `${pos.left}%`;
                token.style.bottom = `${pos.bottom}%`;
            }
        });
    }

    highlightCell(cellNumber, type) {
        const cell = this.boardElement.querySelector(`[data-cell="${cellNumber}"]`);
        if (cell) {
            cell.classList.add(`highlight-${type}`);
            setTimeout(() => cell.classList.remove(`highlight-${type}`), 1000);
        }
    }

    showDiceValue(value) {
        const diceEl = document.getElementById('dice');
        if (!diceEl) return;
        diceEl.innerHTML = '';
        const dots = this.dots[value] || [];
        for (let i = 0; i < 9; i++) {
            const dot = document.createElement('div');
            dot.className = 'dice-dot';
            if (dots.includes(i)) {
                dot.style.opacity = '1';
                dot.style.backgroundColor = '#000';
            } else {
                dot.style.opacity = '0';
            }
            diceEl.appendChild(dot);
        }
    }

    setDiceRolling(isRolling) {
        const diceEl = document.getElementById('dice');
        if (!diceEl) return;
        if (isRolling) diceEl.classList.add('rolling');
        else diceEl.classList.remove('rolling');
    }

    updatePlayersList(players, currentTurn) {
        const list = document.getElementById('players-list');
        if (!list) return;
        list.innerHTML = '';
        players.forEach((p, idx) => {
            const card = document.createElement('div');
            card.className = `player-card ${idx === currentTurn ? 'active-turn' : ''}`;
            card.innerHTML = `
                <div class="player-color-dot" style="background-color: ${p.colorHex}"></div>
                <div class="player-info">
                    <input type="text" class="player-name-input-inline" value="${p.name}" title="Click to rename" maxlength="12" data-id="${p.id}" />
                    <div class="player-position">Pos: ${p.position}</div>
                </div>
            `;
            const input = card.querySelector('.player-name-input-inline');
            if (input) {
                input.addEventListener('change', (e) => {
                    const newName = e.target.value.trim();
                    if (newName && window.app && window.app.isOnline) {
                        window.app.client.send('rename_player', { id: p.id, name: newName });
                    }
                });
                input.addEventListener('keydown', (e) => {
                    if (e.key === 'Enter') {
                        input.blur();
                    }
                });
            }
            list.appendChild(card);
            if (idx === currentTurn) {
                card.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
            }
        });
    }

    showWinner(playerName, playerColor) {
        const overlay = document.getElementById('winner-overlay');
        const title = document.getElementById('winner-title');
        if (title) {
            title.textContent = `${playerName} Wins!`;
        }
        if (overlay) {
            overlay.style.display = 'flex';
            // Trigger reflow for transition
            void overlay.offsetWidth;
            overlay.classList.add('show');
        }
        this.runConfetti();
    }

    hideWinner() {
        const overlay = document.getElementById('winner-overlay');
        if (overlay) {
            overlay.classList.remove('show');
            setTimeout(() => {
                overlay.style.display = 'none';
            }, 500);
        }
        const canvas = document.getElementById('confetti-canvas');
        if (canvas) {
            const ctx = canvas.getContext('2d');
            ctx.clearRect(0, 0, canvas.width, canvas.height);
        }
    }

    showToast(message, type) {
        const container = document.getElementById('toast-container');
        if (!container) return;
        const toast = document.createElement('div');
        toast.className = `toast ${type}`;
        toast.textContent = message;
        container.appendChild(toast);
        setTimeout(() => toast.classList.add('show'), 10);
        setTimeout(() => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 300);
        }, 3000);
    }

    updateStatus(message) {
        const status = document.getElementById('status-bar');
        if (status) status.textContent = message;
    }
    
    runConfetti() {
        const canvas = document.getElementById('confetti-canvas');
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
        
        const particles = [];
        for (let i = 0; i < 100; i++) {
            particles.push({
                x: Math.random() * canvas.width,
                y: Math.random() * canvas.height - canvas.height,
                r: Math.random() * 6 + 2,
                dx: Math.random() * 2 - 1,
                dy: Math.random() * 3 + 2,
                color: `hsl(${Math.random() * 360}, 100%, 50%)`
            });
        }
        
        let animationId;
        const start = Date.now();
        const render = () => {
            if (Date.now() - start > 4000) {
                ctx.clearRect(0, 0, canvas.width, canvas.height);
                cancelAnimationFrame(animationId);
                return;
            }
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            particles.forEach(p => {
                p.x += p.dx + 0.5; // wind
                p.y += p.dy;
                if (p.y > canvas.height) p.y = -10;
                ctx.beginPath();
                ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
                ctx.fillStyle = p.color;
                ctx.fill();
            });
            animationId = requestAnimationFrame(render);
        };
        render();
    }
}

class GameEngine {
    constructor() {
        this.reset();
    }

    reset() {
        this.players = [];
        this.currentTurn = 0;
        this.gameStarted = false;
        this.gameOver = false;
        this.nextPlayerId = 0;
    }

    addPlayer(name) {
        if (this.players.length >= 8) return null;
        const colorObj = PLAYER_COLORS[this.players.length];
        const player = {
            id: this.nextPlayerId++,
            name: name,
            color: colorObj.name,
            colorHex: colorObj.color,
            position: 1
        };
        this.players.push(player);
        return player;
    }

    removePlayer(id) {
        this.players = this.players.filter(p => p.id !== id);
    }

    startGame() {
        if (this.players.length >= 2) {
            this.gameStarted = true;
            this.currentTurn = 0;
            this.gameOver = false;
            this.players.forEach(p => p.position = 1);
            return true;
        }
        return false;
    }

    rollDice() {
        return Math.floor(Math.random() * 6) + 1;
    }

    processMove(playerId, diceValue) {
        const player = this.players.find(p => p.id === playerId);
        if (!player) return null;

        const startPos = player.position;
        let newPos = startPos + diceValue;
        
        // Bounce back if > 100
        if (newPos > 100) {
            newPos = 100 - (newPos - 100);
        }

        let finalPos = newPos;
        let hitSnake = false;
        let hitLadder = false;
        let snakeTo = 0, ladderTo = 0;

        if (SNAKES[newPos]) {
            hitSnake = true;
            snakeTo = SNAKES[newPos];
            finalPos = snakeTo;
        } else if (LADDERS[newPos]) {
            hitLadder = true;
            ladderTo = LADDERS[newPos];
            finalPos = ladderTo;
        }

        const won = finalPos === 100;
        if (won) this.gameOver = true;
        
        player.position = finalPos;

        return {
            newPosition: newPos,
            finalPosition: finalPos,
            hitSnake,
            hitLadder,
            snakeFrom: hitSnake ? newPos : 0,
            snakeTo,
            ladderFrom: hitLadder ? newPos : 0,
            ladderTo,
            won
        };
    }

    nextTurn() {
        if (!this.gameOver && this.players.length > 0) {
            this.currentTurn = (this.currentTurn + 1) % this.players.length;
        }
    }

    getState() {
        return {
            players: this.players,
            currentTurn: this.currentTurn,
            gameStarted: this.gameStarted,
            gameOver: this.gameOver
        };
    }
}

class GameClient {
    constructor(url = 'ws://localhost:8765') {
        this.url = url;
        this.ws = null;
        this.reconnectDelay = 1000;
        this.onConnected = null;
        this.onDisconnected = null;
        this.onMessage = null;
        this.shouldReconnect = true;
    }

    connect() {
        try {
            this.ws = new WebSocket(this.url);
            this.ws.onopen = () => {
                this.reconnectDelay = 1000;
                if (this.onConnected) this.onConnected();
            };
            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    if (this.onMessage) this.onMessage(data);
                } catch (e) {
                    console.error('Error parsing WS message', e);
                }
            };
            this.ws.onclose = () => {
                if (this.onDisconnected) this.onDisconnected();
                if (this.shouldReconnect) {
                    setTimeout(() => this.connect(), this.reconnectDelay);
                    this.reconnectDelay = Math.min(this.reconnectDelay * 2, 30000);
                }
            };
            this.ws.onerror = (err) => {
                console.error('WebSocket error:', err);
                this.ws.close();
            };
        } catch (err) {
            console.error('Failed to create WebSocket:', err);
        }
    }

    send(type, data = {}) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({ type, ...data }));
        }
    }
    
    disconnect() {
        this.shouldReconnect = false;
        if (this.ws) {
            this.ws.close();
        }
    }
}

class SnakesAndLaddersApp {
    constructor() {
        this.engine = new GameEngine();
        this.soundManager = new SoundManager();
        this.renderer = null;
        this.client = new GameClient();
        this.isOnline = false;
        this.isAnimating = false;
        this.messageQueue = [];
        this.isProcessing = false;
    }

    init() {
        const boardEl = document.getElementById('game-board');
        this.renderer = new BoardRenderer(boardEl);
        this.renderer.initBoard();
        this.renderer.showDiceValue(1);
        this.renderer.updateStatus('Waiting for ESP32 connection...');

        // Play Again button (winner overlay)
        const playAgainBtn = document.getElementById('play-again-btn');
        if (playAgainBtn) playAgainBtn.addEventListener('click', () => {
            this.renderer.hideWinner();
        });

        // Add / Rename Player from Web Keyboard
        const addBtn = document.getElementById('add-player-btn');
        const nameInput = document.getElementById('player-name-input');
        
        const handleAddOrRenamePlayer = () => {
            if (!nameInput) return;
            const name = nameInput.value.trim();
            if (name && this.isOnline) {
                if (this.engine.players.length > 0) {
                    this.client.send('rename_player', { id: 0, name });
                } else {
                    this.client.send('add_player', { name });
                }
                nameInput.value = '';
            }
        };

        if (addBtn) addBtn.addEventListener('click', handleAddOrRenamePlayer);
        if (nameInput) {
            nameInput.addEventListener('keypress', (e) => {
                if (e.key === 'Enter') handleAddOrRenamePlayer();
            });
        }

        this.setupClient();
        window.app = this;
    }

    setupClient() {
        this.client.onConnected = () => {
            this.isOnline = true;
            this.renderer.updateStatus('Connected — waiting for players...');
            const statusEl = document.getElementById('connection-status');
            if (statusEl) {
                statusEl.innerHTML = '<span class="status-dot online"></span><span>Online Mode</span>';
            }
        };
        this.client.onDisconnected = () => {
            this.isOnline = false;
            this.renderer.updateStatus('Disconnected — waiting for server...');
            const statusEl = document.getElementById('connection-status');
            if (statusEl) {
                statusEl.innerHTML = '<span class="status-dot offline"></span><span>Offline</span>';
            }
        };
        this.client.onMessage = (msg) => this.handleServerMessage(msg);
        this.client.connect();
    }

    // ── Message Queue ────────────────────────────────────
    // Messages are queued and processed sequentially so
    // animations (dice roll → move → snake/ladder → turn)
    // play out in order without overlap.

    handleServerMessage(msg) {
        this.messageQueue.push(msg);
        if (!this.isProcessing) {
            this.processQueue();
        }
    }

    async processQueue() {
        this.isProcessing = true;
        while (this.messageQueue.length > 0) {
            const msg = this.messageQueue.shift();
            await this.processMessage(msg);
        }
        this.isProcessing = false;
    }

    async processMessage(msg) {
        switch (msg.type) {

            // ── Full state sync (on connect / reconnect) ──
            case 'game_state':
                this.engine.players = msg.players || [];
                this.engine.currentTurn = msg.current_turn || 0;
                this.engine.gameStarted = msg.game_started || false;
                this.engine.gameOver = msg.game_over || false;
                this.syncRender();
                break;

            // ── Player renamed ──
            case 'player_renamed': {
                const targetPlayer = this.engine.players.find(p => p.id === msg.id);
                if (targetPlayer) {
                    targetPlayer.name = msg.name;
                    this.renderer.updatePlayersList(this.engine.players, this.engine.currentTurn);
                    this.renderer.showToast(`Player ${msg.id + 1} renamed to "${msg.name}"`, 'info');
                    if (this.engine.gameStarted && this.engine.currentTurn === msg.id) {
                        this.renderer.updateStatus(`${msg.name}'s turn`);
                    }
                }
                break;
            }

            // ── Player added by ESP32 button ──
            case 'player_added':
                this.engine.players.push(msg.player);
                this.renderer.createToken(msg.player.id, msg.player.colorHex);
                this.renderer.updatePlayersList(this.engine.players, this.engine.currentTurn);
                this.renderer.updateTokenPositions(this.engine.players);
                this.renderer.updateStatus(`${msg.player.name} joined! (${this.engine.players.length} players)`);
                this.renderer.showToast(`${msg.player.name} joined!`, 'info');
                break;

            // ── Game started by ESP32 button ──
            case 'game_started':
                this.engine.players = msg.players;
                this.engine.currentTurn = msg.current_turn;
                this.engine.gameStarted = true;
                this.engine.gameOver = false;
                this.syncRender();
                this.renderer.showToast('Game started!', 'info');
                const firstPlayer = this.engine.players[this.engine.currentTurn];
                if (firstPlayer) {
                    this.renderer.updateStatus(`${firstPlayer.name}'s turn`);
                    this.setActiveToken(firstPlayer.id);
                }
                break;

            // ── Dice rolled by ESP32 button ──
            case 'dice_result': {
                const dicePlayer = this.engine.players.find(p => p.id === msg.player_id);
                this.renderer.updateStatus(`${dicePlayer ? dicePlayer.name : 'Player'} rolled a ${msg.value}!`);

                // Dice animation
                this.soundManager.playDiceRoll();
                this.renderer.setDiceRolling(true);
                for (let i = 0; i < 12; i++) {
                    this.renderer.showDiceValue(Math.floor(Math.random() * 6) + 1);
                    await this.sleep(100 + (i * 10)); // Gradually slow down
                }
                this.renderer.setDiceRolling(false);
                this.renderer.showDiceValue(msg.value);
                await this.sleep(1200); // Pause to let users see the dice roll before moving
                break;
            }

            // ── Token movement ──
            case 'player_moved': {
                const movingPlayer = this.engine.players.find(p => p.id === msg.player_id);
                if (movingPlayer) {
                    const oldPos = movingPlayer.position;
                    await this.renderer.moveToken(msg.player_id, msg.from, msg.to, false);
                    movingPlayer.position = msg.to;
                    this.renderer.updatePlayersList(this.engine.players, this.engine.currentTurn);
                    this.renderer.updateTokenPositions(this.engine.players);
                }
                break;
            }

            // ── Snake hit! ──
            case 'snake_hit': {
                this.soundManager.playSnake();
                this.renderer.showToast(`Snake! ${msg.from} ↓ ${msg.to}`, 'snake');
                this.renderer.highlightCell(msg.from, 'snake');
                await this.sleep(400);

                const snakePlayer = this.engine.players.find(p => p.id === msg.player_id);
                if (snakePlayer) {
                    await this.renderer.moveToken(msg.player_id, msg.from, msg.to, true);
                    snakePlayer.position = msg.to;
                    this.renderer.updatePlayersList(this.engine.players, this.engine.currentTurn);
                    this.renderer.updateTokenPositions(this.engine.players);
                }
                break;
            }

            // ── Ladder hit! ──
            case 'ladder_hit': {
                this.soundManager.playLadder();
                this.renderer.showToast(`Ladder! ${msg.from} ↑ ${msg.to}`, 'ladder');
                this.renderer.highlightCell(msg.from, 'ladder');
                await this.sleep(400);

                const ladderPlayer = this.engine.players.find(p => p.id === msg.player_id);
                if (ladderPlayer) {
                    await this.renderer.moveToken(msg.player_id, msg.from, msg.to, true);
                    ladderPlayer.position = msg.to;
                    this.renderer.updatePlayersList(this.engine.players, this.engine.currentTurn);
                    this.renderer.updateTokenPositions(this.engine.players);
                }
                break;
            }

            // ── Turn changed ──
            case 'turn_changed': {
                this.engine.currentTurn = msg.current_turn;
                const nextPlayer = this.engine.players[msg.current_turn];
                this.renderer.updatePlayersList(this.engine.players, this.engine.currentTurn);
                if (nextPlayer) {
                    this.renderer.updateStatus(`${nextPlayer.name}'s turn`);
                    this.setActiveToken(nextPlayer.id);
                }
                break;
            }

            // ── Game won! ──
            case 'game_won': {
                this.engine.gameOver = true;
                const winner = this.engine.players.find(p => p.id === msg.player_id);
                this.soundManager.playWin();
                this.renderer.showWinner(msg.player_name, winner ? winner.colorHex : '#fff');
                this.renderer.updateStatus(`${msg.player_name} WINS!`);
                break;
            }

            // ── Game reset by ESP32 button ──
            case 'game_reset':
                this.engine.reset();
                this.renderer.hideWinner();
                this.syncRender();
                this.renderer.showDiceValue(1);
                this.renderer.updateStatus('Game reset — waiting for players...');
                this.renderer.showToast('Game reset!', 'info');
                break;

            // ── Error from server ──
            case 'error':
                this.renderer.showToast(msg.message || 'Error', 'snake');
                break;
        }
    }

    // ── Helpers ──────────────────────────────────────────

    sleep(ms) {
        return new Promise(r => setTimeout(r, ms));
    }

    setActiveToken(activePlayerId) {
        this.renderer.tokens.forEach((token, id) => {
            token.classList.toggle('active', id === activePlayerId);
        });
    }

    syncRender() {
        this.renderer.updatePlayersList(this.engine.players, this.engine.currentTurn);
        this.renderer.boardElement.querySelectorAll('.token').forEach(t => t.remove());
        this.renderer.tokens.clear();
        this.engine.players.forEach(p => {
            this.renderer.createToken(p.id, p.colorHex);
        });
        this.renderer.updateTokenPositions(this.engine.players);

        if (this.engine.gameStarted && !this.engine.gameOver) {
            const cp = this.engine.players[this.engine.currentTurn];
            if (cp) {
                this.renderer.updateStatus(`${cp.name}'s turn`);
                this.setActiveToken(cp.id);
            }
        } else if (this.engine.gameOver) {
            this.renderer.updateStatus('Game over!');
        } else if (this.engine.players.length > 0) {
            this.renderer.updateStatus(`${this.engine.players.length} player(s) — press Start`);
        } else {
            this.renderer.updateStatus('Waiting for players...');
        }
    }
}

// ── Boot ─────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
    const app = new SnakesAndLaddersApp();
    app.init();
});

