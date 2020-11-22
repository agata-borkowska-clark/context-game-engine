const canvas = document.getElementById('display')! as HTMLCanvasElement;
const context = canvas.getContext('2d');

function resize() {
  const r = devicePixelRatio;
  canvas.width = r * innerWidth;
  canvas.height = r * innerHeight;
}

resize();
addEventListener('resize', resize);

const socket = new WebSocket('ws://localhost:8000/demo');

socket.addEventListener('open', event => {
  console.log('Connected.');
  socket.send('Hello, World!');
});

socket.addEventListener('message', event => {
  console.log(event.data);
});
