# Fedora RPM

## Pacote atual — 0.6.0-qt.4

O workflow `.github/workflows/qt-linux-client.yml` compila o cliente e empacota o RPM a partir do artefato **do mesmo run**, conferido por SHA-256. Não reutiliza a versão qt.1. A qt.4 inclui os módulos NSS necessários para decifrar SRTP; o CI 37056693735 passou em seis grupos CTest Linux, no teste criptográfico do runtime portátil e no teste do RPM instalado em Fedora 44. O último decifrou/decodificou 51 pacotes de áudio. Também passou em instalação, `rpm -V`, inicialização Qt/motor e remoção. A reprodução no Fedora físico do usuário segue pendente.

O rpmlint atual registrou 1.274 erros e 42 avisos, relacionados às bibliotecas privadas e metadados: continua sendo instalador externo experimental, sem conformidade completa com um repositório oficial Fedora.

```bash
sudo dnf install ./GoLive-P2P-0.6.0-qt.4-fedora-x86_64.rpm
golive-p2p
```

Feche o cliente anterior antes de atualizar. Atualização Linux é manual. [Falha reproduzida e correção](fedora-video-qt4.md).

## Histórico qt.1 — resultado executado em 2026-10-02

CI Fedora Linux 44 (Container Image), run 36968865268: RPM gerado, instalado por `dnf install` sem desativar verificações, arquivos aprovados por `rpm -V`, launcher/menu validados e aplicativo Qt/motor nativo abertos sob Xvfb. Desinstalação por DNF removeu `/opt/golive-p2p`, launcher e desktop entry. GUI executada no usuário root do container; o desktop/som do Fedora físico do usuário não foi testado neste cenário.

RPM: 128.158.515 bytes, SHA-256 `01a36caed48ae0e76a25b02cc65e4734a80fa875a93c5a9d7cc56e0796e3fc50`. Payload nativo corresponde ao tarball publicado com hash fixado. Pacote externo sem assinatura RPM; verificação SHA-256 anexa. Não há repositório DNF nem atualização automática Linux nesta etapa.

O rpmlint foi executado e **não passou como pacote de repositório Fedora**: 1.261 erros e 42 avisos. Classificação dos erros: 870 caminhos privados em `/opt`, 357 bibliotecas sem bit executável (carregamento dinâmico funcionou), 15 RUNPATHs relativos do Qt, seis endereços FSF históricos em licenças, quatro avisos de chamadas setuid em bibliotecas sem privilégio/setuid instalado, quatro detecções de script em avisos de copyright, e casos unitários de script de aviso não executável, shebang env, dependência ALSA explícita, avisos duplicados e grafia de tailnet. São registrados, não ocultados nem anunciados como lint limpo. O RPM é para instalação externa experimental; adequação completa às políticas Fedora e auditoria de dependências continuam pendentes.

O RPM empacota exatamente o cliente Qt/C++ do tarball Linux da pré-release `v0.6.0-qt.1`, conferido pelo SHA-256 fixado no workflow. Não recompila nem substitui os executáveis já publicados. É um instalador de terceiros para teste; não é um pacote dos repositórios oficiais Fedora.

Instala em `/opt/golive-p2p`, com launcher `/usr/bin/golive-p2p`, entrada no menu de aplicativos e ícone. As bibliotecas privadas não são instaladas como bibliotecas do sistema nem registradas como providers RPM. Glibc (mínimo 2.39), interfaces GL/EGL e ALSA continuam sendo dependências do sistema. O DNF resolve essas dependências; Tailscale deve estar instalado e conectado separadamente.

```bash
sudo dnf install ./GoLive-P2P-0.6.0-qt.1-fedora-x86_64.rpm
golive-p2p
```

Para remover:

```bash
sudo dnf remove golive-p2p
```

Atualizações Linux são manuais nesta etapa: instalar o próximo RPM pelo DNF atualizará o mesmo pacote. O instalador continua exclusivamente cliente de recepção. A auditoria completa das dependências/licenças e o teste de áudio/vídeo no Fedora físico do usuário permanecem pendentes; o RPM mantém as licenças/avisos do pacote portátil sem atribuir uma nova licença ao projeto.

Desenvolvimento: em Fedora com `rpm-build` e `desktop-file-utils`, execute `bash native/qt/linux/build-rpm.sh CAMINHO_DO_TARBALL`. O workflow atual é `.github/workflows/qt-linux-client.yml`; o workflow RPM separado usado originalmente na qt.1 foi substituído. O lint é registrado para inspeção; avisos próprios de bibliotecas privadas e de um instalador externo não são anunciados como conformidade com o repositório Fedora.
