# GoLive P2P

Aplicativo desktop para compartilhar tela com até cinco pessoas na mesma tailnet Tailscale. O host coordena a sala por WebSocket no próprio IP Tailscale; vídeo e áudio trafegam por uma conexão WebRTC independente para cada espectador. Não há servidor central de mídia.

## Plataformas e requisitos

### Migração nativa em desenvolvimento (`0.5.0-native.3`)

O motor Windows agora tem implementação C++ com GStreamer: captura automática PrintWindow para janelas e DXGI para monitores, WGC opcional, H.264 com NVENC/fallback OpenH264, áudio por árvore de processos, mistura de aplicativos explicitamente permitidos, conexões WebRTC independentes e decodificação nativa. Electron + React + TypeScript continuam como interface. Consulte [arquitetura e limites](docs/native-media-windows.md) e [validação real](docs/native-validation.md). A versão de desenvolvimento **não é uma release validada**.

A borda WGC no Windows 10 não foi removida nem é anunciada como removida. A prévia é apresentada a 15 FPS e o receptor Windows a 30 FPS, mesmo quando o fluxo de mídia é 60 FPS. O controle de congestionamento por receptor e a revisão completa do isolamento de áudio ainda impedem uma release estável. Pré-releases de teste foram autorizadas explicitamente pelo usuário e descrevem suas limitações. Os parágrafos sobre a release 0.4.0 abaixo descrevem o comportamento anterior.

Uma possível migração futura da interface para **Qt + C++** fica registrada como opção. Ela não foi implementada nesta etapa.

Para preparar o desenvolvimento Windows, use MSVC C++/Windows SDK e PowerShell 7, execute `npm ci` e `npm run native:setup`. O instalador incluirá as dependências nativas e manterá a atualização automática; o usuário final continuará precisando instalar e autenticar o Tailscale separadamente. Linux está priorizado como cliente, com transmissão desabilitada na interface desta etapa.

- Windows 10/11: instalador por usuário, plataforma principal. Vídeo de monitor e janela disponível. Áudio de janela tenta a API por processo no Windows 10 2004 (build 19041) ou posterior; o funcionamento depende das atualizações do Windows e ainda requer validação em dispositivos reais.
- Linux: AppImage. A captura de tela depende do compositor e, em Wayland, do xdg-desktop-portal. O áudio fica desativado até existir filtragem segura do Discord nessa plataforma.
- Tailscale instalado, autenticado e conectado à mesma tailnet em cada computador.
- Para desenvolvimento: Node.js 22+ e npm.

O aplicativo não instala nem configura o Tailscale. Não exige IP público, abertura de portas no roteador, VPS, STUN ou TURN. As ACLs da tailnet e o firewall local precisam permitir TCP 47621 até o host e UDP entre os peers.

## Downloads da versão 0.4.0

Baixe os executáveis completos na [release v0.4.0](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/tag/v0.4.0):

- [Instalador Windows (.exe)](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.4.0/GoLive-P2P-Setup-0.4.0.exe)
- [Linux AppImage](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.4.0/GoLive-P2P-0.4.0.AppImage)
- [Checksums SHA-256](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.4.0/SHA256SUMS-0.4.0.txt)

No Linux, execute `chmod +x GoLive-P2P-0.4.0.AppImage` antes de abrir.

### Atualizações no Windows

Instale a v0.4.0 uma vez pelo executável acima. As versões portáteis v0.3.x não têm atualização automática e exigem essa instalação inicial. Depois disso, o aplicativo consulta as releases públicas do GitHub ao abrir, baixa versões novas em segundo plano e instala automaticamente quando estiver fora de uma sala. Se você estiver em uma sala, a atualização aguarda a saída ou o encerramento do aplicativo. O painel mostra o progresso e oferece **Reiniciar e atualizar** quando a versão estiver pronta. Internet e acesso ao GitHub são necessários para procurar atualizações; a transmissão P2P continua independente disso. O AppImage Linux segue com atualização manual.

## Desenvolvimento e builds

```bash
npm install
npm run dev
npm run lint
npm run typecheck
npm test
npm run build          # instalador Windows
npm run build:linux    # AppImage Linux
npm run build:all      # ambos os pacotes (requer Wine no Linux)
```

Os artefatos ficam em `release/`. Compile preferencialmente cada pacote em sua plataforma e valide a captura em computadores reais. No Windows, use `npm run build`; no Linux, use `npm run build:linux`. Para gerar ambos no Linux, instale Wine. O instalador Windows não exige configuração manual de áudio nem privilégios de administrador; o AppImage pode precisar de `chmod +x`.

## Como usar

1. Em todos os computadores, confirme a conexão com `tailscale status` e `tailscale ip -4`. No PowerShell, se `tailscale` não estiver no PATH, execute `& "$env:ProgramFiles\Tailscale\tailscale.exe" status`.
2. Uma pessoa informa o nome e clica **Criar sala**. O IP Tailscale do host aparece na sala.
3. As demais podem clicar **Procurar salas na tailnet**. A descoberta consulta os dispositivos online que o Tailscale já conhece e procura hosts GoLive na porta 47621. Se a sala não aparecer, informe manualmente o IP do host.
4. Todos aparecem na lista, com ping Tailscale ao lado do nome. Uma pessoa escolhe qualidade e teto de bitrate por espectador, seleciona um monitor inteiro ou uma janela/aplicativo e inicia a transmissão.
5. O transmissor pode alterar a qualidade e o bitrate durante o compartilhamento e acompanhar bitrate real, codec, FPS, resolução, RTT, perda e estado de cada espectador.
6. Ao parar ou sair, os streams e as conexões são limpos. Se a sinalização cair brevemente, o cliente tenta reconectar usando o mesmo ID de sessão, reservado por 30 segundos. O WebRTC tenta nova oferta ICE quando a conexão falha.

Só uma pessoa transmite por vez na sala. Isso evita saturar a banda de upload em grupos pequenos; qualquer participante pode transmitir depois que a transmissão atual parar.

## Qualidade e áudio

Presets: 720p30, 720p60, 1080p30, 1080p60 e 1440p30. São metas de captura. O usuário também pode escolher um teto de bitrate de 0,5 a 20 Mbps por espectador; o WebRTC pode transmitir abaixo dele conforme a rede. Com quatro espectadores, o transmissor envia até quatro fluxos.

Ao compartilhar **uma janela no Windows compatível**, marcar **Áudio do aplicativo (janela)** solicita a captura da árvore de processos da janela selecionada. O helper bloqueia janelas do Discord e do próprio GoLive e interrompe o áudio se um desses aplicativos aparecer como subprocesso da árvore capturada. O microfone não é capturado como entrada. O loopback geral do Electron foi removido para evitar misturar origens de áudio.

No motor C++ atual, ao compartilhar **um monitor**, marque os aplicativos permitidos para misturar somente o áudio deles. Ao escolher uma janela, a lista de monitor não se aplica: o processo da janela e seus subprocessos são usados automaticamente. Chrome, jogos e outros aplicativos identificados são permitidos; Discord e GoLive permanecem excluídos. Nenhum dispositivo de microfone é aberto. Navegadores podem incluir áudio de outras abas e janelas do mesmo aplicativo; a API não separa páginas que compartilham o processo de áudio. Uma chamada Discord dentro do Chrome não pode ser separada de outros sons do Chrome por esta API. Não é usado áudio agregado do monitor. A validação de exclusão com o Discord real tocando simultaneamente continua pendente. No Windows 10 2004 ou posterior, o app tenta iniciar a captura de áudio da janela; se a API não funcionar naquela instalação, mostra o erro e mantém o vídeo sem áudio. Na recepção Linux, áudio Opus é reproduzido pelo Chromium; transmissão Linux continua desabilitada. Uma aba do Discord aberta em navegador não pode ser separada das demais abas pelo processo; portanto, não selecione o navegador como fonte de áudio se ele estiver reproduzindo Discord. A exclusão do Discord **não está garantida** até haver teste em máquinas reais.

A [documentação da Microsoft sobre `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_process_loopback_params) especifica como requisito mínimo o build 20348. Porém, o [projeto `win-capture-audio` do OBS](https://github.com/bozbez/win-capture-audio) relata que a mesma API também funciona em instalações atualizadas do Windows 10 2004 ou posterior. Por isso o app faz a tentativa real de inicialização nessas versões, em vez de rejeitá-las pelo número do build. A documentação oficial não garante esse funcionamento; o app não usa loopback agregado como substituto.

## Ping e reports de logs

O ping ao lado de cada participante é medido pela CLI `tailscale ping` e atualizado periodicamente. `— ms` indica que não foi possível obter resposta. A rota direta ou DERP aparece ao passar o cursor sobre o ping.

**Baixar meu log TXT** abre uma caixa para escolher onde salvar o report local. **Enviar log** permite escolher outro participante. O servidor apenas encaminha o texto; o destinatário vê quem enviou e escolhe **Baixar TXT** ou **Ignorar**. O recebimento não salva arquivos automaticamente. O report contém eventos de informação, avisos e erros da aplicação e pode incluir caminhos locais ou endereços Tailscale; confira antes de enviá-lo.

O report inclui nome, versão, build e arquitetura do sistema operacional. Ao iniciar uma transmissão, registra se a fonte escolhida foi monitor ou janela, se o áudio foi solicitado e o motivo informado pelo helper quando a captura de áudio falhar.

## Borda na captura de janela

No Windows 10, o Windows Graphics Capture usado pelo Chromium desenha uma borda colorida em torno da janela transmitida. O Electron não expõe uma opção confiável para removê-la nessa versão do sistema. A API de captura sem borda depende de recurso introduzido no build 20348 e de permissão específica do Windows. Compartilhar o monitor inteiro evita a borda da janela, mas não oferece o áudio filtrado do aplicativo. A remoção da borda em Windows 10 exigirá uma implementação nativa alternativa de vídeo; o aplicativo não promete removê-la nesta versão.

## Arquitetura

- `src/main`: Electron, seleção de fonte e servidor ligado somente ao IP Tailscale.
- `src/preload`: API limitada via contextBridge, com `contextIsolation: true` e `nodeIntegration: false`.
- `src/shared`: protocolo validado com Zod.
- `src/services/tailscale`: consulta à CLI oficial e descoberta opcional de hosts conhecidos.
- `src/services/signaling`: sala única, limite de cinco sessões, IDs e tokens aleatórios, retomada temporária e roteamento das mensagens pelo socket autenticado.
- `src/services/webrtc`: ponte IPC tipada para o motor C++ no Windows; receptor Chromium no Linux. Uma conexão independente por espectador, sem STUN/TURN.
- `src/services/stats`: getStats por peer a cada 2,5 segundos.
- `native/windows`: motor C++ de captura, H.264/Opus, WebRTC, áudio exclusivo por processo, estatísticas e decodificação. O auxiliar de áudio antigo permanece no repositório, mas não é o motor atual.
- `src/main/logs.ts`: log persistente rotativo e exportação por diálogo nativo.
- `src/main/updater.ts`: atualização do instalador Windows a partir das releases públicas do GitHub.
- `src/renderer`: interface React, diagnóstico e vídeo direto em `HTMLVideoElement`.

O Chromium expõe candidatos ICE locais sem ofuscação mDNS para permitir a negociação do IPv4 Tailscale. Participantes da sala podem ver esses IPs locais no SDP. A mídia usa DTLS/SRTP e a tailnet usa WireGuard/Tailscale.

## Diagnóstico

- `tailscale ping <IP-do-peer>` verifica conectividade. O resultado indica `direct` quando o Tailscale conecta os dispositivos diretamente e `via DERP` quando precisa de relay. Nesse caso o vídeo continua sem passar pelo host de sinalização, mas os pacotes da tailnet podem atravessar o DERP.
- Se a sala não aparecer na descoberta, use o IP manualmente. A descoberta depende de os peers estarem visíveis no `tailscale status --json` e de a porta TCP 47621 estar acessível.
- Se a sinalização conectar mas o vídeo não chegar, verifique as ACLs para UDP entre peers e os estados WebRTC/ICE no painel Diagnóstico.
- No Windows, permita o GoLive P2P no Firewall quando solicitado. No Linux, verifique permissões de captura e xdg-desktop-portal.
- Se a rede cair por mais de 30 segundos, a sessão pode expirar; entre novamente.

## Limitações verificáveis

O fluxo completo entre máquinas, áudio por processo no Windows, seleção de fontes em diferentes ambientes Linux e executáveis empacotados precisam de validação prática nesses sistemas. O ambiente de build automatizado verifica código, protocolo e empacotamento, mas não dispõe de dois desktops conectados à mesma tailnet nem de dispositivos reais de áudio do Windows. A descoberta é opcional e não substitui a entrada manual. Para concluir a funcionalidade de áudio solicitada, é necessário implementar um misturador por processo para monitores e validar a atribuição de sessões em Windows 10 e 11.
