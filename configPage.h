const char configPage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Wi-Fi Setup</title>
  <style>
    :root {
      --base-color: rgb(230, 230, 230);
      --complement: rgb(210, 210, 210);
      --secondary-color: rgb(220, 220, 220);
      --text-color: rgb(80, 80, 80);
      --shadow: rgba(0, 0, 0, 0.1);
      --accent: rgba(31, 117, 254, 0.5);
      --description: rgb(132, 136, 132);
      --red: rgb(136, 0, 0);
    }

    :root.dark {
      --base-color: rgb(35, 35, 35);
      --secondary-color: rgb(45, 45, 45);
      --text-color: rgb(240, 240, 240);
      --complement: rgb(25, 25, 25);
      --shadow: rgba(0, 0, 0, 0.1);
      --accent: rgba(31, 117, 254, 0.5);
      --description: rgb(132, 136, 132);
    }

    * {
      padding: 0;
      margin: 0;
      font-family: 'Poppins', 'sans serif';
      transition: background-color 0.3s ease, color 0.3s ease;
    }

    html, body {
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      background-color: var(--complement);
    }

    .App {
      width: calc(100% - 18px);
      max-width: 400px;
      display: flex;
      justify-content: center;
      padding: 18px;
      background-color: var(--base-color);
      border-radius: 9px;
      box-shadow: var(--shadow);
      flex-direction: column;
      gap: 12px;
    }

    h2, h5 {
      color: var(--text-color);
      text-align: center;
    }

    input {
      width: 100%;
      padding: 9px;
      border-radius: 5px;
      box-sizing: border-box;
      outline: none;
      border: none;
      color: var(--text-color);
      background-color: var(--secondary-color);
    }

    button {
      width: 100%;
      padding: 10px;
      border-radius: 5px;
      border: none;
      color: var(--text-color);
      background-color: var(--complement);
      cursor: pointer;
    }

    button:hover {
      background-color: var(--accent);
    }

    .message {
      text-align: center;
      color: var(--text-color);
      font-size: 16px;
    }
  </style>
</head>
<body>
    <div class="App">
			<h2>Omnisense</h2>
      <h5>Wi-Fi Setup</h5>
      <input id="ssid" placeholder="SSID" type="text" required />
      <input id="password" placeholder="Password" type="password" required />
      <button id="submit">Connect</button>
      <div id="response" class="message"></div>
    </div>

    <script>
      const submitButton = document.getElementById('submit');
      const responseDiv = document.getElementById('response');

      submitButton.addEventListener('click', async () => {
        const ssid = document.getElementById('ssid').value;
        const password = document.getElementById('password').value;

        if (!ssid || !password) {
          alert("Please enter both SSID and password");
          return;
        }

        const configData = {
          ssid: ssid,
          password: password
        };

        try {
          const response = await fetch('/config', {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json'
            },
            body: JSON.stringify(configData)
          });
        } catch (error) {
          responseDiv.textContent = "If the 'omnisense' AP is still available, please try again.";
        }
      });
    </script>
</body>
</html>
)=====";
