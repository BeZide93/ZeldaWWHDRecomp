# Wind Waker HD no Android

[English](android.md) · **Português** · [Español](android.es.md)

The Wind Waker HD roda de forma nativa em dispositivos Android. Não é um emulador: o próprio
código do jogo é recompilado para processadores ARM e desenha com Vulkan. Você precisa da sua
própria cópia do jogo de Wii U; este projeto nunca fornece arquivos do jogo.

## O que a versão de Android faz

| | |
|---|---|
| **Controles na tela** | Os dois analógicos, o direcional, A B X Y, L R ZL ZR e + −. O botão 🎮 embaixo do botão de visão (canto superior esquerdo) mostra ou esconde os controles. Eles somem enquanto um controle está conectado e voltam no próximo toque. Um controle Bluetooth ou USB também funciona. Segure 🎮 para editar o layout: arraste um controle, selecione-o e use Tamanho −/+ para redimensioná-lo; depois toque em Concluir. Redefinir restaura o layout original. As posições e os tamanhos são salvos neste dispositivo e se ajustam à tela após a rotação. A edição solta os controles pressionados e não envia toques ao jogo. |
| **Vibração do telefone** | Em Controls → Phone vibration, **Touch only** é a opção padrão: o telefone vibra com o jogo ao usar os controles por toque. A entrada de um controle ou teclado interrompe a vibração do telefone; usar os controles por toque seleciona o telefone novamente. **Touch or no rumble controller** também vibra o telefone quando nenhum controle conectado tem vibração. **Off** desativa a vibração do telefone; **Rumble** silencia tanto o telefone quanto os controles. Os ajustes de vibração e a opção de resposta tátil do Android são respeitados. `WWHD_PHONE_RUMBLE=0`, `touch` ou `fallback` tem prioridade sobre a opção salva ao iniciar. |
| **A tela do GamePad ao pausar** | Aperte + e a imagem troca para a tela do GamePad (mapa e itens), como o menu de pausa da versão de GameCube. Aperte + de novo para voltar. |
| **60 fps sem deixar o jogo lento** | Nos dispositivos Android rápidos o jogo mostra 60 quadros por segundo. Quando o dispositivo Android esquenta, ele passa sozinho para 30 estáveis, e o jogo nunca fica lento. |
| **Dispositivos Android antigos** | Se o driver da sua GPU Adreno for antigo demais, a tela de erro oferece instalar um driver como o Mesa Turnip. |
| **Recuperação da GPU** | Depois de um erro de dispositivo perdido da GPU, a próxima inicialização identifica o driver e oferece o driver do sistema, outro instalado ou o seletor de ZIP existente antes de iniciar o Vulkan. Continuar mantém sua escolha; um teste inicial incompleto ainda usa o driver do sistema. |
| **Saves** | Exporte e importe seus saves e guarde no backup do Android. Saves de um Wii U de verdade (americano ou europeu) também funcionam. |

O Android solicita 60 Hz para 30 fps, 60 fps interpolados e 60 fps reais, ou a cadência selecionada de 120/240 Hz para interpolação mais rápida. O Android pode escolher outra frequência compatível. A solicitação é reaplicada ao voltar ao jogo. Para deixar a escolha da frequência inteiramente com o Android, defina `WWHD_NO_60HZ=1` em uma compilação de desenvolvimento (`0` mantém a solicitação ativada).

## Celulares testados

Medido com o medidor do próprio jogo na parte mais pesada da Ilha de Outset (cerca de 3.900
desenhos por quadro). *Velocidade do jogo* são os passos de lógica do jogo por segundo: 30 é a
velocidade total.

| Aparelho | Processador / GPU | Quadros por segundo | Velocidade do jogo | Situação |
|---|---|---|---|---|
| Galaxy S25 Ultra | Snapdragon 8 Elite, Adreno 830 | cerca de 59 fps | 29,7 / 30 | ótimo |
| Galaxy Z Fold 8 | SM8850, Adreno 840 | 60 fps, 30 quando quente | 26–30 / 30 | ótimo, nas duas telas |
| Lenovo Legion Tab | Snapdragon 8 Gen 3, Adreno 750 | 30 fps estáveis | 28,7–30 / 30 | bom |
| OnePlus 8 Pro | Snapdragon 865, Adreno 650 (Mesa Turnip) | 30 fps | 26–30 / 30 | roda; um travamento de GPU ainda em análise |
| Galaxy Tab S6 Lite | Snapdragon 720G, Adreno 618 (Mesa Turnip), 4 GB | 15–24 fps | 15–24 / 30 | roda devagar: cerca de metade da velocidade em Outset |

Você precisa de um dispositivo Android de 64 bits com Android 13 ou mais novo e Vulkan 1.3 (ou um driver
personalizado na Adreno), e alguns GB livres.

## Mirar com o giroscópio do celular

Abra **Settings > Controls > Gyro** e escolha **Device gyro (phone/tablet)**.
A opção vem desligada e exige giroscópio e acelerômetro. Deixe o celular parado
por um segundo para calibrar. Ative **Options > Gyro** no jogo e mire em primeira
pessoa sem mover o analógico direito. Os eixos acompanham a tela nas duas posições
horizontais. Funciona com controles de toque ou com um controle conectado.
Os sensores param em segundo plano e enquanto os ajustes bloqueiam o jogo;
ao voltar, deixe o celular parado novamente. Sensibilidade, inversão e recalibração
estão no [guia do giroscópio](gyro.md). A linha de estado informa se faltam sensores.

## Como jogar

### O app de instalação (sem PC)

O app de instalação vem em todas as versões oficiais: baixe o `WindWakerHD-<versão>-android-arm64.apk` na
página de [Releases](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases) (o APK `android-x86_64` é para emuladores e Chromebooks). Ele não leva nenhum
código do jogo: monta o jogo no seu dispositivo Android a partir da sua cópia. As próximas versões instalam por
cima e mantêm os saves e as configurações (o dispositivo Android prepara o jogo de novo quando uma atualização
precisar).

1. Instale o APK. Se você já tem uma versão montada no PC, exporte o save antes (toque e segure o
   ícone do app) e desinstale essa versão: as duas são assinadas de jeitos diferentes.
2. Abra o app e escolha o seu jogo: **Choose extracted game folder** (pasta extraída), **Choose WUA
   archive** (arquivo `.wua`) ou **Choose WUD / WUX disc image** (imagem de disco; depois também
   **Choose disc key file** e **Choose common key file**, as chaves).
3. Toque em **Start / resume / retry setup**. O dispositivo Android extrai e compila o jogo uma vez só; a tela
   mostra o progresso, por exemplo "Compiling: 30/80 · about 12 min left". Você pode sair do app
   enquanto isso, e **Pause setup** pausa com segurança.
4. Quando aparecer que a instalação terminou, toque em **Play current build**.

### Ou: monte no seu PC

1. Instale o Android SDK e o NDK, o JDK 17 ou mais novo, CMake, Ninja e Python 3.
2. Extraia o seu jogo e gere o código dele (passos 1 e 2 de *Building* no [README](../README.md)).
3. Rode `android/build_native.sh` e depois `./gradlew assembleRelease` dentro de `android/`.
4. Instale o APK pela USB com `adb install -r app/build/outputs/apk/release/app-release.apk`.

Os passos completos estão no README, na seção *Android (build it yourself)*.

## Jogo limpo

- Use a sua própria cópia do jogo, tirada do seu disco ou console.
- Um APK montado no seu PC contém o seu jogo recompilado. Use só no seu dispositivo Android e nunca compartilhe.
- Se alguém oferecer um APK pronto com o jogo dentro, ele não é deste projeto.

## Perguntas

**Preciso de um controle?**
Não. Os controles na tela cobrem o GamePad inteiro. Um controle também funciona e esconde os
botões de toque enquanto estiver conectado.

**Qual versão do jogo?**
A versão de Wii U do The Wind Waker HD, americana ou europeia. Saves de um Wii U de verdade também
funcionam.

**Meu dispositivo Android esquenta. É normal?**
O jogo é pesado. Quando o dispositivo Android esquenta, o jogo cai sozinho de 60 para 30 fps estáveis e
continua na velocidade total.

**Alguma coisa não funciona.**
Abra uma [issue](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues) com o modelo do dispositivo Android,
a versão do Android e o que aconteceu (pode escrever em inglês ou português). Depois de um
travamento, na próxima vez que você abrir o jogo ele oferece o relatório do travamento para
compartilhar. Isso também cobre encerramentos por falta de memória em primeiro plano, sinais e ANRs
registrados pelo Android, quando o sistema disponibiliza o histórico. O resumo inclui motivo, data,
status/sinal, importância do processo e a última amostra de memória PSS/RSS. Omite descrições, traces,
bytes de estado do processo e logcat para evitar nomes, dados de saves ou caminhos privados.
Saídas normais, liberação de memória em segundo plano e paradas pelas ferramentas de saves não geram
um relatório do sistema. Um relatório nativo próximo no tempo é incluído no mesmo arquivo, se houver.
Compartilhar é opcional.

---

O port de Android é de [rhemfur](https://github.com/rhemfur) e faz parte do ZeldaWWHDRecomp.
Projeto de fã, sem ligação com a Nintendo e sem aprovação dela. The Legend of Zelda e The Wind
Waker são marcas da Nintendo.

Para diagnósticos de falhas de desenvolvimento, `WWHD_CRASH_GUEST_MEMORY=1` adiciona uma janela limitada de 128 bytes por registo convidado apenas quando a janela completa pertence a uma secção RPX executável e não gravável. Uma referência executável é preservada durante o carregamento, após as cópias do carregador. Na falha, leituras voláteis limitadas comparam os 128 bytes convidados atuais com essa referência; apenas uma correspondência completa mostra bytes da referência. Código modificado ou indisponível é totalmente ocultado, sem bytes alterados ou hashes. A amostra não é atómica em relação a escritas convidadas; heap, pilha, dados graváveis, intervalos desconhecidos e limites de secção são ocultados. Não são adicionados ASCII, nomes de threads ou conteúdo de jogos guardados. O relatório inclui uma captura numérica recente das regiões de endereços convidados, número de threads registadas e RSS do processo (pode estar indisponível). A opção está desativada por padrão e assim não consulta memória nem copia imagens. O código original preservado está limitado a 16 MiB e 64 secções sem sobreposições; intervalos maiores ou ambíguos ficam ocultos. Os bytes de código originais podem identificar a versão do jogo; reveja o relatório antes de partilhar.
