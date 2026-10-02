# Validação nativa nesta máquina

Data: 2026-10-01. Base inicial: `3dfae85`. Build em desenvolvimento: `0.5.0-native.1`.

## Ambiente observado

- Windows 10 Pro 22H2, **10.0.19045.6466**, x64.
- NVIDIA GeForce RTX 2060, driver `32.0.16.1088`; também há adaptador virtual Parsec.
- Dois monitores enumerados como `DISPLAY1` e `DISPLAY2`.
- GStreamer 1.26.7 MSVC x64, runtime/SDK verificados pelos SHA-256 oficiais fixados no script.
- MSVC 14.50.35717 e Windows SDK 10.0.26100.0.
- Tailscale ativo; IP local 100.75.12.74. Fedora 100.94.54.119 está disponível segundo o usuário e tinha GoLive 0.3. Não havia serviço de sinalização na porta 47621 durante a sondagem inicial.

## Executado

Os arquivos brutos ficam em `work/native-results` e `work/native-probe` (não publicados, pois incluem nomes de janelas, PIDs e IPs). `native/windows/probe.mjs` cria dois processos nativos e encaminha somente SDP/ICE; os pacotes de mídia passam diretamente entre eles.

| Cenário                                                                | Evidência obtida                                                                                          | Limite                                                                                                  |
| ---------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Monitor principal, DXGI, NVENC, 1920×1080/60, teto 6 Mbps              | 797 frames codificados em 13,286 s (~60 FPS); receptor decodificou JPEGs; ICE/DTLS conectados             | Dois processos no mesmo PC; sem validação entre computadores                                            |
| Segundo monitor, DXGI, NVENC, 1920×1080/60                             | 738 frames em 12,294 s (~60 FPS), vídeo recebido                                                          | Não é teste de mudança de monitor durante uma sessão                                                    |
| Janela WGC coberta por outra janela de teste                           | Frames permanecem verdes (alvo), sem a janela vermelha que a cobre                                        | Janela Win32 de teste; não generaliza para jogos/DRM/janelas minimizadas                                |
| Redimensionamento da janela WGC                                        | Frames continuam após mudar a janela de 900×640 para 650×460                                              | Saída mantém o preset; revisão de proporção da apresentação foi necessária                              |
| Áudio da janela de teste, 440 Hz gerados por waveOut no mesmo processo | API process-loopback iniciou no build 19045.6466; receptor recebeu 723.840 frames de áudio e RMS não nulo | Não prova isolamento em qualquer aplicativo                                                             |
| Monitor com lista explícita permitindo somente o executável de teste   | Uma origem nativa admitida; áudio recebido (660.000 frames no ponto de medição)                           | Mistura com múltiplos aplicativos e exclusão simultânea do Discord ainda não validadas                  |
| Bitrate alterado de 6 para 2 Mbps                                      | Comando aplicado ao encoder durante o fluxo                                                               | Falta medir adaptação e congestionamento por receptor                                                   |
| Remover/recriar conexão                                                | Novas conexões chegaram a connected e o receptor voltou a entregar frames                                 | Foram encontrados candidatos ICE precoces; fila nativa foi adicionada e requer repetição final          |
| OpenH264 por CPU                                                       | Fluxo nativo conectou e entregou frames após correção do keyframe inicial                                 | Um erro posterior de estatísticas do receptor foi corrigido; benchmark final pendente                   |
| Interface Electron real                                                | Criou sala pelo IP Tailscale e exibiu prévia nativa com largura 960                                       | Primeiro script encontrou falha de import ESM do updater, corrigida; limpeza do script também corrigida |
| Verificações TypeScript                                                | Lint, typecheck e 29 testes passaram                                                                      | Repetir depois das últimas correções e antes de empacotar                                               |
| Auditoria npm                                                          | Zero vulnerabilidades após Electron 44.5.1 e Vitest 5.0.3                                                 | Não equivale à auditoria das bibliotecas nativas                                                        |

Uma amostra de `nvidia-smi` durante o teste mostrou GPU 49%, encoder 9%, decoder 13%, 1.694 MiB. Havia outros aplicativos ativos, incluindo reprodução de vídeo; esses valores **não são um benchmark atribuído ao GoLive**. A medição isolada de CPU/GPU e latência fim a fim permanece pendente.

## Ainda não validado / bloqueios

- Windows 11 real; máquina não disponível nesta sessão até este registro.
- Remoção da borda WGC em Windows 10. O atributo solicitado não comprova remoção; não há solução sem borda garantida para janela coberta.
- Dois computadores reais e depois cinco participantes reais. O Fedora informado pelo usuário será usado para teste do cliente; resultado ainda pendente.
- Áudio simultâneo de Discord e aplicativo permitido, árvores dinâmicas, processos órfãos/renomeados, falhas de identificação e garantia de exclusão de microfone reproduzido por outro aplicativo.
- Controle de congestionamento independente por receptor.
- Prévia local é 15 FPS; apresentação Windows remota é 30 FPS em JPEG. O encode/fluxo testado é 60 FPS. Renderização direta de GPU ainda não foi implementada.
- Latência, carga sustentada, reconexão de rede/Tailscale e sinalização entre dispositivos.
- Inspeção final do instalador, execução do pacote e checksum; atualização efetiva entre duas versões instaladas.
- Auditoria de todas as licenças/DLLs/plugins antes de publicar o pacote Windows.

`native-validation.json` conserva os gates como false. Nenhuma release deve anunciar a migração Windows como concluída com essas pendências. Uma pré-release **somente cliente Linux** poderá ser publicada, a pedido do usuário, para viabilizar os testes no Fedora, com limitações explícitas.

## Atualização dos testes de 2026-10-01

Resultados abaixo substituem o status pendente dos cenários correspondentes acima; os demais limites continuam válidos.

- **Dois computadores reais:** Windows 10 em 100.75.12.74 transmitiu ao Fedora em 100.94.54.119. O usuário confirmou “chegou video”. Estatísticas nativas registraram H.264 e candidatos UDP de mídia nos dois IPs da tailnet; conexão WebSocket de sinalização observada entre eles. Áudio estava desativado nesse teste. Não foi medida latência fim a fim. `crossDevice` passou a true apenas por este cenário.
- **Quatro receptores locais independentes:** cinco processos nativos, 790 frames codificados em 13,18 s (~60 FPS); os quatro peers conectaram e decodificaram frames. Não equivale a cinco computadores nem inclui o servidor de sala/limite da interface nesse teste.
- **WGC com áudio e reconexão, após fila ICE:** janela Win32 coberta/redimensionada, 1.128 frames em 18,83 s; áudio recebido e recriação do peer conectada; relatório sem erros. Atribuição foi exercitada com o processo de teste, não com Discord tocando simultaneamente.
- **Fallback CPU repetido:** OpenH264 conectou, entregou vídeo e reconectou sem erro de estatísticas. Codificou 137 frames em 12,30 s (~11 FPS) no teste 1080p60. É funcional, mas não sustenta o preset 60 FPS nesta medição.
- **Aplicativo empacotado:** criou sala, exibiu prévia nativa de 960 pixels e identificou `GPU · NVENC`. Seletor enumerou seis janelas, quatro miniaturas disponíveis e nenhuma fonte desabilitada. Miniaturas são apenas imagens do seletor fornecidas pelo Electron; captura/transmissão continuam nativas. Algumas janelas não oferecem miniatura e mostram uma indicação explícita.
- **Duplo clique:** teste CDP de entrada/saída de tela cheia passou no aplicativo real. Uma repetição durante uso simultâneo falhou por estado/posição de interação; não foi tratada como validação adicional. O script pode coletar estatísticas sem mexer na apresentação com `--stats-only`.
- **CPU atribuída ao GoLive:** amostra de 10,085 s da árvore de processos do aplicativo durante o fluxo ao Fedora: 2,33% da capacidade total de 20 processadores lógicos. Motor: 4,297 s de CPU e ~159 MB de working set no ponto observado. É uma amostra curta; não constitui benchmark sustentado nem mede GPU/latência.
- **Instalador:** extraído o NSIS e seu `app-64.7z`; payload com 839 arquivos/692.573.576 bytes. Verificados 763 arquivos nativos pelo manifesto SHA-256, executáveis e configuração do updater. Checksum daquele build: `1282da68756a3932ec5a9bb6188c38e5d1e27e64040e2e98e309c4e377ccae93`. Correções posteriores de C++ exigem novo build e nova conferência; gate global permanece bloqueado.
- **Análise MSVC:** `/analyze:only` encontrou verificações de null/handles/COM e uso de pilha excessivo na consulta de processos. Corrigidos os apontamentos, reforçado bloqueio apenas do áudio em falha e identidade original do processo proprietário da janela. Repetir análise e teste de áudio após essas correções.
- **Cliente Linux:** pré-release [v0.5.0-native.1](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/tag/v0.5.0-native.1), exclusivamente AppImage de recepção, compilado no CI e executado no Fedora pelo usuário. SHA-256 `3ea4aed2c78b1c557b6f0c748aaece241b2f6e8e8361e941ae3329e8f62c69a3`. Sem instalador/manifesto de atualização Windows nesta pré-release.

### Integração real de avaliação com OBS 32.2.2

Download oficial Windows x64 conferido pelo digest SHA-256 `4d6e40e3ab155f56b30de517380566a206d74b63cdf5ad49aa596924768f97e1`. Um programa C++ separado (`native/windows/obs-probe.cpp`) carregou `obs.dll`, D3D11 e `win-capture.dll`, inicializou vídeo e recebeu frames BGRA da libobs; Qt/OBS frontend não foi iniciado. É uma avaliação, não um backend embarcado no GoLive.

- Hook `game_capture`: após corrigir o diretório de execução para resolver os helpers/hooks, 1.019 frames de composição, zero conteúdo no alvo Win32 GDI e largura da fonte zero. Não resolve janelas comuns.
- `window_capture` BitBlt no alvo GDI: 1.019 frames, 959 amostras centrais verdes e largura final 634 após redimensionar. Resultado limitado ao alvo GDI de teste.
- BitBlt no Chrome real: imagens aos 8 e 16 segundos mostram somente moldura/título e cliente cinza, sem página/vídeo. O contador “não preto” sozinho teria produzido um falso positivo; inspeção visual foi necessária. **Não adotar como equivalente ao WGC para janela coberta.**
- Consulta WinRT real nesta máquina retornou false para `GraphicsCaptureSession.IsBorderRequired`. O OBS também consulta a disponibilidade dessa propriedade antes de desativar a borda. A exigência geral de janela coberta sem borda no Windows 10 continua não atendida; mudar a interface para Qt não altera essa API.

Na etapa anterior à integração PrintWindow, a captura padrão escolhia DXGI para monitor e WGC para janela; o resultado atualizado consta abaixo. Métodos ficam em opções avançadas. Codificação é automática, GPU primeiro e CPU em falha, com identificação durante a transmissão. A libobs não foi adotada após esses resultados.

### Captura PrintWindow integrada — 2026-10-01

Seleção automática agora usa DXGI para monitores e PrintWindow/PW_RENDERFULLCONTENT para janelas. O auxiliar C++ é supervisionado por Job Object e usa memória compartilhada privada; após três segundos sem frames é encerrado. Falha posterior encerra a captura e gera erro explícito. O caminho WGC continua disponível e pode ser usado como fallback inicial. Não existe garantia universal para aplicativos que não implementam PrintWindow; cursor ainda não é composto nesse método.

- Fixture Win32 coberta e redimensionada: 1.068 frames em 18,43 s (~58 FPS), NVENC, áudio do processo recebido, peer recriado e conectado, relatório sem erros. Imagem recebida inspecionada: conteúdo verde do alvo, sem a janela vermelha que o cobria.
- Chrome real com vídeo, coberto pelo alvo de teste: 837 frames em 18,42 s (~45 FPS), NVENC, áudio desligado, reconexão e imagem recebida com conteúdo de vídeo correto. Não sustenta 60 FPS nesse caso; o redimensionamento desse teste atingiu a fixture, não o Chrome.
- Observação do desktop durante PrintWindow da fixture: screenshot físico aos três segundos mostra o alvo sem borda amarela nesta máquina Windows 10. Essa validação se limita à fixture; não altera a resposta IPC de garantia global (`borderRemovalVerified: false`).
- Lint, typecheck e 33 testes passaram. Sete verificações nativas de política de áudio passaram. Análise MSVC do motor e auxiliar PrintWindow passou sem apontamentos. Teste de aplicativo deliberadamente travado ainda pendente.

Relatórios e imagens de teste ficam em work/native-results (não versionados). Os gates de release permanecem bloqueados pelos cenários ainda não executados e pela auditoria de distribuição.


Instalador reconstruído após PrintWindow: extraídos NSIS e payload app-64.7z, conferidos 764 arquivos nativos pelo manifesto, incluindo window-capture.exe, app.asar e configuração do updater. SHA-256: e890cfb968ead82c85fe3debea172c6b83cf035de4dea5469a99659468609e0c. Build Windows concluído. Não foi publicado como migração concluída; instalação efetiva e atualização entre duas versões continuam pendentes.

### Pré-release Windows 0.5.0-native.2

Publicação de teste autorizada explicitamente pelo usuário, sem afirmar conclusão da migração. Instalador NSIS extraído e payload conferido: 764 arquivos nativos, executáveis e updater. SHA-256 0ff7cdf31f83dbe8628333bdc8d20efbc385b542cbd7f09c7ad13dc19f24329d. Aplicativo empacotado abriu sala e prévia nativa. Lint, typecheck e 33 testes passaram.

Teste adicional window-lifecycle-test.mjs encerrou somente o auxiliar filho do motor de teste: falha detectada em 791 ms, IPC continuou responsivo, captura foi reiniciada e parada com sucesso. Uma tentativa de provocar bloqueio via WM_PRINT não bloqueou PrintWindow neste sistema; não foi registrada como teste de hang aprovado. O teste de bloqueio real permanece pendente. Os demais gates continuam false. Esta pré-release não contém manifesto para atualização automática de instalações estáveis.

Teste posterior à preparação da pré-release: window-lifecycle-test.mjs --freeze-helper suspendeu somente as threads do auxiliar pertencente ao motor de teste. Falha foi detectada em 4.118 ms incluindo o lançamento do PowerShell; IPC respondeu e nova captura foi iniciada/parada. Isso valida o watchdog contra auxiliar congelado, não identifica um aplicativo real que bloqueie internamente PrintWindow. Relatório work/native-results/window-frozen-helper.json.

## Áudio exclusivo — atualização de 2026-10-02

Corrigido o relógio dos buffers PCM/Opus: timestamps agora derivam do contador de amostras de cada peer, e o envio usa timer Windows de alta resolução. O relógio anterior seguia os despertares da thread; no teste Chromium, 601 pacotes foram descartados e 475.713/867.360 amostras foram reconstruídas pelo receptor, mesmo com perda de rede zero. Após a correção, teste equivalente recebeu 1.697 pacotes, descartou 1 e reconstruiu 7.266/868.320 amostras (~0,84%). Energia RTP e PCM não zero confirmados, com vídeo no mesmo elemento de reprodução.

Testes reais locais C++ → Chromium (não equivalem a Fedora):
- Fixture Win32 com tom de 440 Hz: áudio e vídeo juntos, RMS ~0,0033, 542 quadros decodificados/apresentados no teste 720p30.
- Chrome real com tom de 660 Hz enquanto a fixture toca 440 Hz: vídeo e áudio recebidos, RMS ~0,0061, 540 quadros. Componente espectral 660/440 ~26,85. A API capturou a árvore do Chrome; não foi usado áudio agregado.
- Captura inversa (fixture enquanto Chrome toca): RMS ~0,0033, 533 quadros, componente 440/660 ~24,47. O teste exige que o tom selecionado supere o outro por fator 20; isso é uma medição com codec com perdas, não garantia geral de isolamento em qualquer aplicativo.
- Fixture de teste renomeada Discord.exe: vídeo continuou (546 quadros), energia de áudio RTP e RMS zero. É simulação da política por nome, não o Discord real.
- Receptor antigo do commit 0060121 também recebeu áudio+vídeo com o relógio corrigido. A correção preventiva de faixas sem stream no Linux não foi reproduzida como causa deste caso; o problema de temporização nativa foi medido e corrigido.
- Minecraft Java real: janela capturada corretamente em PrintWindow, javaw.exe PID 28152 (o minecraft.exe da lista era launcher). Pacotes de áudio chegaram ao receptor nativo, mas RMS final ~0,000016; a imagem inspecionada mostra o menu de pausa. Não registramos áudio de gameplay nem áudio Minecraft→Fedora como aprovados.

Chrome e outros aplicativos identificados agora são permitidos, a pedido explícito do usuário. Navegadores incluem áudio da árvore do aplicativo, podendo incluir outras abas/janelas; Discord web dentro do navegador não pode ser separado por esta API. Discord e GoLive como processos separados e hosts compartilhados/sem identidade permanecem bloqueados. Foram corrigidos erros WASAPI ignorados, retentativa após revalidação, limpeza COM/handles e a consulta de filhos com identidade inacessível. PID/criação da janela são fixados antes de iniciar vídeo/áudio. A interface distingue captura em silêncio de som detectado e os logs registram RMS/contagem de frames não silenciosos.

Validação: 35 testes TypeScript, 18 testes C++ de falhas WASAPI/retry/política e 9 verificações nativas de política. Lint, typecheck e análise MSVC passaram. Nova instalação, atualização entre versões, áudio real no Fedora e exclusão do Discord real simultâneo ainda pendentes; gates globais continuam bloqueados.
