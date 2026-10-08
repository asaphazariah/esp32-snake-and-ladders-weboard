import asyncio
import http.server
import threading
import json
import os
import random
import websockets

SNAKES = {
    25: 3, 42: 1, 56: 48, 61: 43,
    92: 67, 94: 12, 98: 80
}
LADDERS = {
    7: 30, 16: 33, 20: 38, 36: 83,
    50: 68, 63: 81, 71: 89, 86: 97
}
PLAYER_COLORS = [
    {'name': 'Red',    'color': '#e74c3c'},
    {'name': 'Blue',   'color': '#3498db'},
    {'name': 'Green',  'color': '#2ecc71'},
    {'name': 'Yellow', 'color': '#f1c40f'},
    {'name': 'Purple', 'color': '#9b59b6'},
    {'name': 'Orange', 'color': '#e67e22'},
    {'name': 'Pink',   'color': '#e91e90'},
    {'name': 'White',  'color': '#ecf0f1'},
]
MAX_PLAYERS = 8

class GameState:
    def __init__(self):
        self.players = []       # list of {id, name, color, colorHex, position}
        self.current_turn = 0
        self.game_started = False
        self.game_over = False
    
    def add_player(self, name):
        if self.game_started:
            return None
        if len(self.players) >= MAX_PLAYERS:
            return None
        
        player_id = len(self.players)
        color_info = PLAYER_COLORS[player_id]
        
        player = {
            'id': player_id,
            'name': name,
            'color': color_info['name'],
            'colorHex': color_info['color'],
            'position': 1
        }
        self.players.append(player)
        return player
    
    def start_game(self):
        if len(self.players) >= 2:
            self.game_started = True
            self.current_turn = 0
            self.game_over = False
            for player in self.players:
                player['position'] = 1
            return True
        return False
    
    def roll_dice(self):
        return random.randint(1, 6)
    
    def process_move(self, player_id, dice_value):
        player = self.players[player_id]
        from_pos = player['position']
        target_pos = from_pos + dice_value
        
        # Handle > 100 bounce back
        if target_pos > 100:
            target_pos = 100 - (target_pos - 100)
            
        new_position = target_pos
        final_position = target_pos
        hit_snake = False
        hit_ladder = False
        won = False
        
        # Check snakes/ladders
        if target_pos in SNAKES:
            final_position = SNAKES[target_pos]
            hit_snake = True
        elif target_pos in LADDERS:
            final_position = LADDERS[target_pos]
            hit_ladder = True
            
        player['position'] = final_position
        
        if final_position == 100:
            won = True
            self.game_over = True
            
        return {
            'from_pos': from_pos,
            'new_position': new_position,
            'final_position': final_position,
            'hit_snake': hit_snake,
            'hit_ladder': hit_ladder,
            'won': won
        }
    
    def next_turn(self):
        self.current_turn = (self.current_turn + 1) % len(self.players)
    
    def reset(self):
        self.players = []
        self.current_turn = 0
        self.game_started = False
        self.game_over = False
    
    def to_dict(self):
        return {
            'players': self.players,
            'current_turn': self.current_turn,
            'game_started': self.game_started,
            'game_over': self.game_over
        }

# Global state
game_state = GameState()
connected_clients = set()

async def broadcast(message):
    """Send message to all connected clients"""
    if connected_clients:
        data = json.dumps(message)
        await asyncio.gather(*[client.send(data) for client in connected_clients], return_exceptions=True)

async def handle_client(websocket):
    connected_clients.add(websocket)
    try:
        # Send current game state to new client
        await websocket.send(json.dumps({
            'type': 'game_state',
            **game_state.to_dict()
        }))
        
        async for raw_message in websocket:
            try:
                message = json.loads(raw_message)
            except json.JSONDecodeError:
                continue
                
            msg_type = message.get('type')
            
            if msg_type == 'add_player':
                if not game_state.game_started and not game_state.game_over:
                    if len(game_state.players) >= 8:
                        # Rotate back to 1 player instead of erroring
                        game_state.reset()
                        player = game_state.add_player('Player 1')
                        await broadcast({
                            'type': 'game_state',
                            **game_state.to_dict()
                        })
                    else:
                        name = message.get('name', f'Player {len(game_state.players) + 1}')
                        player = game_state.add_player(name)
                        if player:
                            await broadcast({'type': 'player_added', 'player': player})
            
            elif msg_type == 'rename_player':
                player_id = message.get('id')
                new_name = str(message.get('name', '')).strip()
                if new_name and isinstance(player_id, int) and 0 <= player_id < len(game_state.players):
                    game_state.players[player_id]['name'] = new_name
                    await broadcast({
                        'type': 'player_renamed',
                        'id': player_id,
                        'name': new_name,
                        'players': game_state.players
                    })
            
            elif msg_type == 'start_game':
                if game_state.start_game():
                    await broadcast({
                        'type': 'game_started',
                        'players': game_state.players,
                        'current_turn': game_state.current_turn
                    })
                else:
                    await websocket.send(json.dumps({'type': 'error', 'message': 'Need at least 2 players'}))
            
            elif msg_type == 'roll_dice':
                if not game_state.game_started or game_state.game_over:
                    continue
                
                player_id = game_state.current_turn
                dice_value = game_state.roll_dice()
                
                # Broadcast dice result
                await broadcast({'type': 'dice_result', 'player_id': player_id, 'value': dice_value})
                
                # Process move
                result = game_state.process_move(player_id, dice_value)
                
                # Small delay for animation
                await asyncio.sleep(0.1)
                
                # Broadcast movement
                await broadcast({
                    'type': 'player_moved',
                    'player_id': player_id,
                    'from': result['from_pos'],
                    'to': result['new_position']
                })
                
                # Check snake/ladder
                if result['hit_snake']:
                    await asyncio.sleep(0.5)
                    await broadcast({
                        'type': 'snake_hit',
                        'player_id': player_id,
                        'from': result['new_position'],
                        'to': result['final_position']
                    })
                elif result['hit_ladder']:
                    await asyncio.sleep(0.5)
                    await broadcast({
                        'type': 'ladder_hit',
                        'player_id': player_id,
                        'from': result['new_position'],
                        'to': result['final_position']
                    })
                
                # Check win
                if result['won']:
                    await asyncio.sleep(0.5)
                    await broadcast({
                        'type': 'game_won',
                        'player_id': player_id,
                        'player_name': game_state.players[player_id]['name']
                    })
                else:
                    game_state.next_turn()
                    await broadcast({
                        'type': 'turn_changed',
                        'current_turn': game_state.current_turn
                    })
            
            elif msg_type == 'reset_game':
                game_state.reset()
                await broadcast({'type': 'game_reset'})
    
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        connected_clients.discard(websocket)

def start_http_server(directory, port=8080):
    os.chdir(directory)
    handler = http.server.SimpleHTTPRequestHandler
    server = http.server.HTTPServer(('0.0.0.0', port), handler)
    print(f'HTTP server running at http://localhost:{port}')
    server.serve_forever()

async def main():
    # Get project directory
    project_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Start HTTP server in daemon thread
    http_thread = threading.Thread(
        target=start_http_server,
        args=(project_dir, 8080),
        daemon=True
    )
    http_thread.start()
    
    # Start WebSocket server
    print(f'WebSocket server running at ws://localhost:8765')
    async with websockets.serve(handle_client, '0.0.0.0', 8765):
        print('\n=== Snakes & Ladders Mock Server Ready! ===')
        print('    Open http://localhost:8080 in your browser')
        print('    Press Ctrl+C to stop\n')
        await asyncio.Future()  # run forever

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print('\nServer stopped.')
