// Unused vftable slots: declared so that the useful virtual methods land at their exact slot. A real virtual
// method yields `call [rax+slot*8]`, like in the game (dl::vcall yields `mov rdx,[rax+…] ; call rdx`).
#pragma once

#define DL_VPAD1(n) virtual void _vpad_##n();
#define DL_VPAD10(n) DL_VPAD1(n##0) DL_VPAD1(n##1) DL_VPAD1(n##2) DL_VPAD1(n##3) DL_VPAD1(n##4) \
                     DL_VPAD1(n##5) DL_VPAD1(n##6) DL_VPAD1(n##7) DL_VPAD1(n##8) DL_VPAD1(n##9)
