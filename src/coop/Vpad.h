// Places de vftable non utilisées : déclarées pour que les méthodes virtuelles utiles tombent à leur place
// exacte. Une vraie méthode virtuelle donne `call [rax+place*8]`, comme dans le jeu (dl::vcall donne
// `mov rdx,[rax+…] ; call rdx`).
#pragma once

#define DL_VPAD1(n) virtual void _vpad_##n();
#define DL_VPAD10(n) DL_VPAD1(n##0) DL_VPAD1(n##1) DL_VPAD1(n##2) DL_VPAD1(n##3) DL_VPAD1(n##4) \
                     DL_VPAD1(n##5) DL_VPAD1(n##6) DL_VPAD1(n##7) DL_VPAD1(n##8) DL_VPAD1(n##9)
