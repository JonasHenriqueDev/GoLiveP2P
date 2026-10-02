# Motor nativo Windows — implementação e critérios de aceite

## Estado

O upstream clonado em 2026-10-01 estava em `3dfae85` e não continha este documento. A sessão anterior havia descrito um processo C++ separado, mas seu documento não foi publicado no remoto. A implementação nesta cópia mantém Electron/React/TypeScript para sala, sinalização, atualização e interface; o processo C++ assume captura, áudio, H.264/Opus, WebRTC, decodificação e medição. Não há servidor de mídia.

Não confundir o build de desenvolvimento `0.5.0-native.1` com uma migração validada para release. Os testes e bloqueios constam em `native-validation.md`.

## Bibliotecas escolhidas

| Opção                     | Integração examinada                                                                                                                                                                                                                                     | Decisão                                             |
| ------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------- |
| libobs                    | Inicialização `obs_startup`, fontes e encoders são plugins. Não fornece uma ponte pronta para a malha WebRTC atual; seria necessário adaptar frames/texturas e administrar módulos e suas dependências. A política de áudio também exige código próprio. | Não adotada nesta implementação.                    |
| libwebrtc                 | APIs nativas cobrem PeerConnection; o build oficial usa GN/Ninja/depot_tools e grande checkout do Chromium. Uma integração correta exigiria adaptar captura, texturas/frames, áudio, renderização e manter a revisão do SDK e toolchain.                 | Não incorporada como dependência não verificada.    |
| GStreamer 1.26.7 MSVC x64 | SDK oficial verificado por SHA-256. Plugins reais carregados e pipelines executados nesta máquina. `d3d11screencapturesrc`, NVENC/OpenH264, H.264/RTP, Opus, `webrtcbin`, libnice, DTLS/SRTP e decodificadores.                                          | Implementado por APIs C chamadas pelo processo C++. |

Referências primárias: [OBS Core](https://docs.obsproject.com/reference-core), [plugins OBS](https://docs.obsproject.com/plugins), [libwebrtc nativo](https://webrtc.googlesource.com/src/+/main/docs/native-code/development/), [captura D3D11](https://gstreamer.freedesktop.org/documentation/d3d11/d3d11screencapturesrc.html), [webrtcbin](https://gstreamer.freedesktop.org/documentation/webrtc/), [SDK fixado](https://gstreamer.freedesktop.org/data/pkg/windows/1.26.7/msvc/).

### Avaliação executável de libobs

`pwsh -File native/windows/obs-evaluation.ps1 -Mode game` baixa arquivos oficiais OBS 32.2.2 com digests fixados, compila `obs-probe.cpp` e exercita libobs/D3D11/win-capture contra a janela Win32 de teste. Requer MSVC e `npm run native:build` previamente para gerar a fixture. `-Mode bitblt -Chrome` avalia a janela Chrome disponível. O programa gera frames de composição e amostras BMP para inspeção; número de frames ou pixels não pretos não comprova captura correta.

Este programa é separado do build/instalador. Ele usa headers GPL do download oficial, configura um cabeçalho local mínimo para o probe e resolve APIs dinamicamente na DLL da mesma versão; não constitui um SDK vendorizado nem altera a licença do motor principal. O plugin OBS pode atualizar hooks em ProgramData e registros Vulkan, comportamento observado em seu código, portanto a avaliação deve ser executada conscientemente em ambiente de desenvolvimento.

Resultados reais e limitações estão em [native-validation.md](native-validation.md): BitBlt capturou a fixture GDI, mas perdeu o conteúdo do Chrome; game capture não produziu conteúdo para a fixture. WGC do OBS verifica a mesma propriedade WinRT de controle de borda que não está disponível no build 19045 desta máquina. **libobs não foi incorporada como solução universal sem borda após esses testes.**

GStreamer é dinamicamente ligado. O pacote inclui DLLs, plugins, scanner, inventário de hashes e licenças do SDK. A presença das licenças não substitui a revisão das obrigações de redistribuição. O SDK completo contém componentes opcionais GPL; a release está bloqueada até concluir essa revisão e eliminar dependências dispensáveis. A versão está fixada para reproduzir os testes; atualizar o SDK requer repetir os testes.

## Captura, codificação e apresentação

- Enumeração nativa de HWND/HMONITOR. O identificador é validado contra fontes existentes imediatamente antes de iniciar.
- A seleção automática usa DXGI para monitor e PrintWindow (`PW_RENDERFULLCONTENT`) para janela. WGC permanece disponível nas opções avançadas e como fallback inicial quando PrintWindow falha. Janela/DXGI e monitor/PrintWindow são recusados no IPC.
- PrintWindow roda em um executável C++ separado, com memória compartilhada privada, handles herdados restritos e Job Object. O motor encerra o auxiliar após três segundos sem frames; travar o aplicativo capturado não deve travar o processo de mídia. Redimensionamento é tratado pelo auxiliar. O método depende do suporte do aplicativo, não garante todas as janelas e não inclui cursor atualmente. Não é BitBlt da tela.
- Não há fallback silencioso para GDI/BitBlt nem promessa de equivalência para janelas cobertas.
- O Windows 10 pode manter a borda WGC. `show-border=false` é solicitado; não é prova de que o sistema a removeu. O controle documentado do plugin depende de Windows 11. Não há bypass implementado para Windows 10.
- H.264 usa NVENC D3D11 quando disponível e OpenH264 em CPU quando explicitamente escolhido ou se o hardware falhar. Adaptadores AMD/Intel usam o fallback funcional por enquanto; Media Foundation/QSV não foram integrados como aceleradores sem testes.
- Há uma captura/encode compartilhada e uma pipeline RTP/DTLS/SRTP/ICE independente por receptor, limitada a quatro receptores. Sem STUN/TURN. A qualidade e o teto de bitrate são aplicados ao encoder compartilhado; não há controlador de congestionamento independente por receptor concluído. Essa diferença em relação ao Chromium precisa ser resolvida antes da release.
- O encoder entrega SPS/PPS em keyframes e fornece um keyframe inicial aos peers novos. Caps RTP são negociadas antes de criar SDP.
- Prévia local JPEG de até 15 FPS e apresentação remota JPEG de até 30 FPS via IPC, separadas do fluxo de rede que pode ser 60 FPS. A decodificação é nativa; o Electron apresenta imagens e mantém layout responsivo. Esse caminho de apresentação tem custo adicional e não equivale a renderização direta de textura D3D11 em 60 FPS. Medir antes de anunciar latência baixa.
- Áudio recebido é decodificado no motor e reproduzido por WASAPI. O medidor RMS não é uma prova de isolamento entre aplicativos.

## Áudio e política de atribuição

`process-audio.hpp` utiliza exclusivamente `ActivateAudioInterfaceAsync` com `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` e `INCLUDE_TARGET_PROCESS_TREE`. Não inicializa captura de microfone nem loopback agregado de endpoint.

Para janela, o HWND determina o PID. Para monitor, a interface permite uma lista explícita de caminhos de executáveis apresentados pelas sessões de **renderização** WASAPI. O motor acompanha sessões em todos os endpoints de reprodução ativos, adiciona/remove capturas e mistura PCM no C++. Árvores sobrepostas são deduplicadas. Uma origem sem identidade confiável fica fora da mistura; o vídeo continua.

PID e horário de criação são conferidos; o handle do processo é mantido para detectar saída/reutilização. A árvore é revista antes de enfileirar amostras. Discord/variantes, GoLive/Electron e hosts de áudio compartilhados do sistema são bloqueados. Navegadores e outros aplicativos identificados são permitidos a pedido do usuário. Para navegador, a atribuição é ao aplicativo e sua árvore, não a uma aba; áudio de páginas do mesmo navegador pode ser compartilhado e Discord web não pode ser separado por esta API. Processos sem caminho/horário de criação acessível são bloqueados. Uma falha encerra e limpa aquela origem. Falha da API produz aviso e silêncio; nunca ativa uma fonte agregada.

Limites: listas por nome/caminho não identificam por si só todos os aplicativos renomeados, carregamentos de conteúdo de terceiros ou processos órfãos. Corridas de criação/saída precisam de teste e reforço de política. Um aplicativo pode reproduzir áudio de microfone que ele próprio capturou; a API não marca a proveniência interna de suas amostras. Não há garantia geral de exclusão de Discord ou microfone sem validar esses casos. A política deve continuar conservadora e a release permanece bloqueada.

## IPC e ciclo de vida

`stdin/stdout` privados do child process transportam NDJSON versão 1. `stderr` é reservado aos logs das bibliotecas. Zod valida pedidos, eventos e resultados no preload/main. O motor recusa métodos desconhecidos, limites de qualidade, fontes inválidas, SDP/candidatos excessivos e linhas maiores que 1 MiB. O supervisor limita respostas a 3 MB, fila a 128 pedidos, tem deadline de startup e de pedido e rejeita pendências ao encerrar. Nenhuma string arbitrária de pipeline sai da interface.

Métodos: `capabilities`, `sources`, `audio-sessions`, `start`, `offer`, `signal`, `remove`, `stop`, `bitrate`, `stats`. Eventos: `ready`, `signal`, `state`, `frame`, `audio-state`, `warning`, `error`. Os dados de mídia local não entram no WebSocket. O servidor existente valida a sinalização e só aceita ofertas do transmissor registrado.

EOF pede encerramento; o supervisor finaliza um processo travado após o prazo. Parar cancela a captura, workers de áudio e peers. A interface registra a intenção de transmissão na sala antes de criar ofertas. Mudança de qualidade reinicia pipelines e renegocia; mudança de bitrate é aplicada ao vivo. Reconexão de sinalização continua usando token e janela de retomada existentes.

## Build e release

Windows: Node, PowerShell 7 e MSVC C++/SDK para desenvolver; `npm run native:setup` baixa/extrai o SDK com hashes fixados e compila. `npm run dev` e `npm run build:win` recompilam o motor. Em uma máquina de usuário, o instalador inclui o runtime; não é preciso instalar SDK ou Visual Studio. Tailscale continua requisito separado.

`native/windows/runtime/manifest.json` contém hashes dos arquivos. O instalador NSIS e o updater existentes foram preservados. `scripts/verify-package.mjs` deve conferir arquivos empacotados e hashes. A publicação exige evidências reais em `native-validation.json`; não basta o runner compilar.

Linux continua usando o receptor Chromium nesta etapa, com transmissão desabilitada na interface. Um AppImage só deve ser declarado testado depois de build/execução num ambiente Linux disponível.
