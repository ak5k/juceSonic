#define EEL_LICE_GET_FILENAME_FOR_STRING(idx, fs, p) (((SX_Instance*)opaque)->GetFilenameForParameter(idx, fs, p))
#define EEL_LICE_GET_CONTEXT(opaque)                                             \
    (((opaque) && GetCurrentThreadId() == ((SX_Instance*)opaque)->m_main_thread) \
         ? (((SX_Instance*)opaque)->m_lice_state)                                \
         : NULL)

#define EEL_LICE_STANDALONE_NOINITQUIT
#define EEL_LICE_WANT_STANDALONE
#include <WDL/eel2/eel_lice.h>