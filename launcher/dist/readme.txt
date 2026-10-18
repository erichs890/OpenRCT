Last updated:    {DATE}
------------------------------------------------------------------------


Table of contents
-----------------
1.0) Sobre
2.0) Contato
 * 2.1) Reportando bugs
3.0) Plataformas suportadas
4.0) Instalando e rodando o OpenRCT
 * 4.1) Controles no jogo
 * 4.2) Linha de comando
5.0) Desenvolvimento
6.0) Licença
7.0) Privacidade
8.0) Créditos

1.0) Sobre
---- -----
OpenRCT {VERSION} é uma reconstrução em C, função por função, do
RollerCoaster Tycoon (Deluxe, 1999/2003), escrito por Chris Sawyer, para
preservação. O jogo original roda com as funções já reconstruídas no lugar,
cada uma verificada contra o original. Projeto de fã, não oficial.

O jogo NÃO vem junto: é preciso ter a sua cópia original.

2.0) Contato
---- -------
https://github.com/erichs890/OpenRCT

2.1) Reportando bugs
---- ---------------
Abra uma issue em https://github.com/erichs890/OpenRCT/issues dizendo o que
aconteceu, como reproduzir e, se der, anexe o save (.SV4).

3.0) Plataformas suportadas
---- ----------------------
Windows 10 e 11 (o exe é de 32 bits e roda em sistemas de 64 bits).
Por enquanto só a versão Steam do RollerCoaster Tycoon Deluxe; o launcher
confere o SHA-256 do RCT.EXE.

4.0) Instalando e rodando o OpenRCT
---- ------------------------------
1. Extraia esta pasta em qualquer lugar.
2. Abra openrct.exe.
3. Na primeira vez, escolha a pasta do jogo (a da Steam é achada sozinha:
   C:\Program Files (x86)\Steam\steamapps\common\RollerCoaster Tycoon Deluxe).
4. Clique em Jogar.

As opções ficam em OpenRCT.ini, ao lado do exe. Nada é escrito na pasta do
jogo além dos saves do próprio jogo.

4.1) Controles no jogo
---- -----------------
  Roda do mouse        zoom do mapa (até 4x mais perto que o original)
  Ctrl + roda          zoom da tela (Ctrl+0 = automático)
  F11 / Alt+Enter      tela cheia

4.2) Linha de comando
---- ----------------
  openrct.exe --play                pula o menu
  openrct.exe --open arquivo.SV4    abre um save ou cenário (ou arraste o
                                    arquivo para cima do exe)
  openrct.exe --dir <pasta>         pasta do jogo
  openrct.exe --classic             tela original 640x480
  openrct.exe --no-hooks            só o código original, sem a reconstrução

5.0) Desenvolvimento
---- ---------------
Código-fonte, testes e documentação de cada função reconstruída:
https://github.com/erichs890/OpenRCT

6.0) Licença
---- -------
OpenRCT é licenciado sob a licença MIT (veja licence.txt). Ela cobre só o
OpenRCT, não o RollerCoaster Tycoon nem os arquivos do jogo.

7.0) Privacidade
---- -----------
Veja PRIVACY.md.

8.0) Créditos
---- --------
Veja contributors.md. RollerCoaster Tycoon é de Chris Sawyer.
