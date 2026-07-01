/*
 * st_decode_context.h — optional decode-side hooks for lazy frame fill callbacks.
 */

#ifndef ATARILIB_ST_DECODE_CONTEXT_H_
#define ATARILIB_ST_DECODE_CONTEXT_H_

/*
 * Clip bounds registered by a lazy frame-fill callback. Owned by the sprite cache (or other
 * decode orchestrator); not passed through Bftp_ExArgs.
 */
struct ClipBounds {
	bool valid = false;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;

	void reset()
	{
		valid = false;
	}

	void set(int crop_x, int crop_y, int crop_w, int crop_h)
	{
		x = crop_x;
		y = crop_y;
		w = crop_w;
		h = crop_h;
		valid = true;
	}
};

/*
 * Non-owning facade passed into lazy frame-fill callbacks. The owner binds it to a ClipBounds
 * slot for the duration of one decode; if the callback never calls set_clip_bounds(), crop is
 * scanned at runtime.
 *
 * C++ has no Java-style anonymous classes. Lambdas (optionally via std::function) are the usual
 * substitute for captured closures, but this handle keeps the callback ABI a plain C function
 * pointer with no heap allocation or vtables.
 */
class IDecodeContext {
public:
	static IDecodeContext bind(ClipBounds *bounds)
	{
		IDecodeContext ctx;
		ctx.bounds_ = bounds;
		return ctx;
	}

	void set_clip_bounds(int crop_x, int crop_y, int crop_w, int crop_h)
	{
		if (bounds_ != nullptr) {
			bounds_->set(crop_x, crop_y, crop_w, crop_h);
		}
	}

private:
	ClipBounds *bounds_ = nullptr;
};

#endif /* ATARILIB_ST_DECODE_CONTEXT_H_ */
