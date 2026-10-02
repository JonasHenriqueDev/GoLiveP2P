# Fedora sem vídeo — correção 0.6.0-qt.4

O log enviado pelo usuário em 2026-10-02 mostra Fedora 44, Qt 6.8.3 e GoLive 0.6.0-qt.3: sala conectada e WebRTC `connected`, mas nenhum decoder de áudio nas estatísticas. A conexão estabelecida não comprova recebimento de imagens.

## Falha reproduzida

Em Windows 10 build 19045.6466, foi executado o motor nativo GStreamer 1.26.7 com captura de uma janela Win32 de teste, coberta/redimensionada, NVENC 1080p60. Um segundo processo, dentro de Fedora 44/Docker, recebeu sinalização IPC e mídia WebRTC do Windows usando exatamente o motor/bibliotecas do tarball 0.6.0-qt.3. A sinalização deste teste foi encaminhada diretamente entre os processos; não equivale a um teste entre dois computadores da tailnet.

Sem corrigir o runtime, ICE/DTLS conectou, o Windows enviou RTP, mas o receptor apresentou **zero imagens**. O debug de `srtpdec` registrou `Failed to create the stream (err: 5)` e descartou os pacotes. O `libsrtp2.so.1` incluído no pacote usa NSS; o empacotamento copiava dependências visíveis ao `ldd`, mas omitira os módulos NSS carregados dinamicamente.

Foram acrescentados `libsoftokn3.so`, `libfreebl3.so`, `libfreeblpriv3.so` e `libnssckbi.so` da mesma distribuição/build de `libnss3.so`, seguidos pela resolução recursiva de suas dependências. O hash da biblioteca NSS do pacote publicado coincidiu com a distribuição usada no teste. Nenhuma política criptográfica do Fedora foi alterada.

Com esse runtime completo, a repetição entregou centenas de JPEGs de vídeo decodificado 1920×1080 pelo IPC e amostras de áudio Opus com RMS positivo. O teste com saída PulseAudio virtual não comprova continuidade audível ou reprodução no dispositivo físico do usuário; o contador de áudio avançou menos que o tempo total e continua sendo uma limitação a investigar. Não há declaração de áudio perfeito.

## Proteção contra regressão

`native/windows/srtp-runtime-test.cpp` executa Opus/RTP → SRTP criptografado → RTP autenticado/decifrado → áudio decodificado. A chave fixa é exclusivamente material público de teste. Em Fedora 44 com bibliotecas privadas:

- Módulos NSS ausentes: falhou, **zero pacotes** decodificados.
- Runtime completo: passou, **51 pacotes** decodificados.

O CI agora executa esse teste com o runtime empacotado e novamente com o RPM realmente instalado no Fedora, além da inicialização Qt, verificação e remoção do pacote. Os avisos/licenças NSS/NSPR são incluídos.

O run final `37056693735`, código `79ddbcd4424605bcaeb3a0a46411084472ab43c7`, passou nos seis grupos de testes Linux, no teste SRTP portátil e no teste SRTP do RPM instalado (51 pacotes). A repetição direta Windows → Fedora/Docker com o tarball final qt.4 decodificou 992 frames 1920×1080 e enviou centenas de JPEGs ao consumidor IPC; o SRTP registrou 3.912 pacotes recebidos e zero descartados. Os artefatos e suas somas SHA-256 foram conferidos.

Windows qt.4: lint, build/testes Qt, análise MSVC e conferência dos 868 arquivos do instalador extraído/instalado passaram. Um teste Qt Windows de áudio em paralelo ao teste Docker registrou 14 underflows de captura; esse cenário **não passou** como teste de continuidade. Não deve ser apresentado como áudio validado sob carga concorrente.

Repetição isolada com o executável instalado (`2026-10-02T20-00-44-809Z`): passou, 539 frames de prévia, 486 apresentados, 692.160 frames de áudio, zero underflows, descartes de FIFO ou intervalos de envio perdidos. Esse resultado não invalida a falha sob carga concorrente nem substitui audição no Fedora físico.

O log compartilhado pelo botão de bug também inclui as últimas estatísticas completas. O diagnóstico periódico informa transporte, quadros decodificados e contadores SRTP, sem registrar chaves de mídia. A interface aguarda um quadro apresentado antes de anunciar recebimento da transmissão.

## Ainda precisa de confirmação

Instalar o RPM 0.6.0-qt.4 no Fedora físico do usuário, conectar ao host Windows e confirmar vídeo, áudio e continuidade audível. O teste Docker reproduziu e corrigiu uma falha concreta do pacote, mas não substitui essa confirmação. Os testes de cinco dispositivos e Windows 11 permanecem pendentes.

Referências: [módulos NSS](https://firefox-source-docs.mozilla.org/security/nss/build.html), [SRTP no GStreamer](https://gstreamer.freedesktop.org/documentation/srtp/srtpdec.html).
