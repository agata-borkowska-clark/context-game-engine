const socket = new WebSocket('ws://localhost:8000/demo');

socket.addEventListener('open', event => {
  console.log('Connected.');
  socket.send('Hello, World!');
});

socket.addEventListener('message', event => {
  console.log(event.data);
});
