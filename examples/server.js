const http = require("http");

// Simple HTTP server
const server = http.createServer((req, res) => {

  // Basic endpoint
  if (req.url === "/hello") {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("Hello World\n");
    return;
  }

  // Chunked encoding endpoint
  if (req.url === "/chunk") {
    // Do NOT set Content-Length → Node automatically uses chunked encoding
    res.writeHead(200, {
      "Content-Type": "text/plain",
      "Transfer-Encoding": "chunked",
    });

    let count = 0;

    const interval = setInterval(() => {
      count++;
      res.write(`chunk #${count}\n`);

      if (count === 5) {
        // End stream
        clearInterval(interval);
        res.end("end\n");
      }
    }, 1000);

    return;
  }

  // Server-Sent Events (SSE)
  if (req.url === "/sse") {
    // Required headers
    res.writeHead(200, {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache",
      "Connection": "keep-alive",
      // "Transfer-Encoding": "chunked" is automatic
    });

    res.write("\n"); // Open the stream

    let id = 0;

    const interval = setInterval(() => {
      id++;
      res.write(`event: message\n`);
      res.write(`id: ${id}\n`);
      res.write(`data: message ${id}\n\n`);
    }, 1000);

    // If client disconnects → stop interval
    req.on("close", () => {
      clearInterval(interval);
      console.log("SSE client disconnected");
    });

    return;
  }

  // Unknown path
  res.writeHead(404, { "Content-Type": "text/plain" });
  res.end("Not found\n");
});

// Start server
server.listen(8080, () => {
  console.log("Server listening on http://localhost:8080");
  console.log("Endpoints:");
  console.log("  /hello");
  console.log("  /chunk");
  console.log("  /sse");
});
