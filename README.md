# GoLive P2P

Compartilhamento de janela ou monitor com até cinco participantes pela mesma tailnet Tailscale. A versão `0.6.0-qt.1` usa **somente Qt Widgets + C++ na interface**, com captura, áudio e WebRTC no motor nativo C++. Electron, React e TypeScript foram retirados do aplicativo e do build de produção. As versões anteriores permanecem no histórico do Git.

## Downloads e instalação

A versão Qt está disponível como **pré-release experimental**, com instalador Windows completo, cliente Linux de recepção e checksums SHA-256 na [release v0.6.0-qt.1](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/tag/v0.6.0-qt.1). Não anuncia conclusão de todos os critérios de mídia. Consulte [resultados reais e limites](docs/qt-validation.md).

Windows: execute o instalador EXE. As DLLs Qt, o runtime C++, os executáveis de captura e as bibliotecas de mídia são incluídos. Não é necessário instalar Qt, Visual Studio, Node ou GStreamer para usar. A primeira troca da versão Electron para Qt exige instalar esse EXE; o pacote Qt instala em diretório separado. Feche o GoLive anterior para liberar a porta da sala. Linux: extraia o pacote `linux-x64.tar.gz` e execute `golive`; essa etapa é exclusivamente cliente de recepção.

O **Tailscale deve estar instalado, autenticado e conectado separadamente** em cada computador. GoLive não exige servidor central de mídia, IP público, abertura de portas no roteador, STUN ou TURN. ACLs/firewall precisam permitir TCP 47621 até o host e UDP entre os participantes.

## Uso

1. Informe seu nome e crie uma sala no Windows. O host abre a sala no próprio IP Tailscale. Outros participantes entram pelo IP ou usam a descoberta de salas.
2. Selecione qualidade e bitrate, clique **Escolher janela ou monitor** e selecione a fonte. O seletor mostra prévias do compositor Windows quando disponíveis.
3. Para janela, o áudio segue o aplicativo e seus subprocessos automaticamente. Para monitor, marque os aplicativos permitidos. Origens desconhecidas/excluídas bloqueiam apenas o áudio.
4. A captura escolhe o método automaticamente. Opções avançadas permitem seleção explícita. Codificação usa NVENC quando disponível e fallback OpenH264 em CPU, identificado nas informações.
5. Duplo clique no vídeo alterna tela cheia. O painel mostra estatísticas e ping; logs podem ser salvos ou enviados a outro participante por ação explícita.

Só uma pessoa transmite por vez. Vídeo e áudio usam conexões WebRTC independentes para cada espectador, com uma captura/codificação compartilhada. O WebSocket transmite apenas sinalização e mensagens de sala/logs. O limite é cinco participantes, incluindo o host.

## Captura e áudio: limites atuais

Windows 10/11 são o foco. A seleção automática usa DXGI para monitor e PrintWindow/PW_RENDERFULLCONTENT para janela; WGC fica disponível e pode ser fallback inicial. PrintWindow preservou conteúdo coberto nos testes documentados, mas depende do aplicativo. **Não há garantia universal de ausência de borda amarela no Windows 10**: WGC pode mantê-la. Mudar a interface para Qt não altera essa restrição do sistema.

Áudio de janela utiliza process-loopback WASAPI, exclusivamente para a árvore de processos selecionada; monitor mistura árvores explicitamente permitidas, sem loopback agregado. Discord, GoLive e hosts compartilhados/sem identidade são bloqueados. Não é aberta entrada de microfone. Navegadores podem incluir outras abas/janelas do mesmo aplicativo; a API não separa Discord web de outros sons do mesmo navegador. Não há garantia geral de exclusão de conteúdo reproduzido internamente por um aplicativo.

NVENC está integrado e testado nesta máquina NVIDIA. Outras GPUs usam CPU por enquanto. Prévia local é 15 FPS e apresentação remota usa JPEG até 30 FPS; o fluxo de rede pode ser 60 FPS. Renderização D3D11 direta entre processos foi experimentada e falhou; não integra esta versão. Congestionamento independente por receptor, Windows 11 real, áudio entre Windows/Fedora Qt, Discord real simultâneo, gameplay Minecraft, atualização efetiva entre instalações e auditoria completa de dependências continuam pendentes. O [documento técnico](docs/native-media-windows.md) explica as decisões de bibliotecas.

## Atualização

O cliente Qt Windows procura novas releases **do canal Qt**, baixa o instalador por HTTPS e exige tamanho e SHA-256 conferidos com o digest do asset GitHub. Aguarda sair da sala para instalar; o botão permite reiniciar e atualizar quando o download estiver pronto. A primeira migração do Electron é manual. A atualização entre duas versões Qt instaladas ainda precisa de teste real; Linux segue com atualização manual.

## Desenvolvimento

Requisitos Windows: MSVC C++/Windows SDK, PowerShell 7, Python 3.13 para obter o SDK Qt 6.8.3 MSVC x64, NSIS 3 para o instalador. Node/npm são apenas atalhos de desenvolvimento e scripts de conferência, sem dependências de runtime.

```powershell
npm run setup
npm run native:setup
npm run dev
npm run lint
npm run typecheck   # compilação C++ e testes Qt
npm test
npm run test:integration
npm run build:win
npm run verify:package
```

O executável de desenvolvimento fica em `release/qt-unpacked`; o instalador em `release/`. A estrutura do Qt está em `native/qt`; o motor Windows e os testes WASAPI estão em `native/windows`. O mesmo motor compila em Linux com captura desabilitada, para recepção nativa H264/Opus. No Linux, use `QT_ROOT` apontando para Qt 6.8 e `npm run build:linux`, com os pacotes de desenvolvimento GStreamer/WebRTC e CMake/Ninja instalados. O CI compila/testa o cliente Qt Linux e confere suas dependências.

As licenças Qt e das bibliotecas de mídia são incluídas no pacote. A conferência de arquivos não substitui a auditoria completa das obrigações de distribuição. Resultados devem indicar cenários executados e pendências; nenhuma pré-release de teste equivale à validação completa da migração.
