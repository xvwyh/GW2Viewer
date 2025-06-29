#pragma once

#define CHECK_ASYNC do { if (!context || context->Cancelled) { context.reset(); return; } } while (false)
#define CHECK_SHARED_ASYNC do { if (!context || context->Cancelled) { context->Cancel(); return; } } while (false)
