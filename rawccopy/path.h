#ifndef PATH_H
#define PATH_H

#include "context.h"
#include "ut-wrapper.h"
#include "byte-buffer.h"

typedef struct {
	bytes original;			//raw buffer containing an index_entry describing the step in the path
	bytes dereferenced;		//when original is a link, ie a reparse point, raw buffer containing
							//an index_entry describing the derefereced step (the target of the link in 'original')
} *path_step;

#define OriginStep(step) (step->original) 
#define DerefStep(step) (step->dereferenced ? step->dereferenced : step->original) 

typedef UT_array* resolved_path;

bool TryParsePath(const execution_context context, const wchar_t* path, resolved_path *result);

resolved_path CopyPath(const resolved_path pt);

inline void DeletePath(resolved_path pt) { utarray_free(pt);}

resolved_path EmptyPath(const execution_context context);

bool GoDown(const execution_context context, resolved_path pt, const wchar_t* item);

bool GoUp(resolved_path pt);

#endif //! PATH_H