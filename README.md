<h1 align = 'center'> Потылицин Никита БПИ228 </h1>
<h2 align = 'center'> Демонстрация HW10</h2>

![bandicam-2024-05-24-13-56-21-521](https://github.com/nikitaptl/works_ACS_OS/assets/145208333/999dddf7-0aed-41b6-bb45-e941d11d4de9)

- Первым запускается Клиент №2, он выводит свой ip-адрес и порт (чтобы сервер мог перенаправлять ему сообщения) и ждёт внешнего подключения от сервера
- Затем запускается сервер, на вход ему поступает **его** порт, адрес и порт Клиента №2. Далее сервер ждёт внешнего подключения от Клиента №1
- В конце запускается Клиент №1, на вход ему поступает Ip и порт сервера. Он подключается к серверу, и начинается общее взаимодействие
