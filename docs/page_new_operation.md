@page new_operation Implement new operations
@brief Guide for implementing new operations from the application or other libraries

@note The files in `src/op_*.c` contain complete examples of operations.

@note This process is the same if you wish to implement new operations inside your application or
for contributing new operations to libmpix.

## Register an operation

An operation only needs one macro in a C file to be declared, and no header is needed:

```c
MPIX_REGISTER_OP(<custom>, P_PARAM_NAME_1, P_PARAM_NAME_2...)
```

The parameter names are optional.

For this macro to be scanned by `genlist.sh`, you need to add this line in CMakeLists.txt,
before the call to `libmpix_init(<project>)`:

```
libmpix_add_genlist_source(${CMAKE_CURRENT_LIST_DIR}/op_<custom>.c)
```

## Create the new type struct

The way to add a new operation is to call:

All libmpix operations use a common base defined in @ref mpix_operation_h, the @ref mpix_base_op.

- It describes the operation so that it can be selected from a list of operations available.

- It keeps track of the ongoing state of the processing.

It is possible to define a wrapper struct around @ref mpix_base_op to define extra fields
to help doing this.

For instance:

```c
struct mpix_operation {
	struct mpix_base_op base;
	/* Any other custom fields */
	uint8_t param_1;
	uint8_t param_2;
};
```

## Define the adder function

This function will be called whenever one more input line is available and can be processed.

```c
int mpix_add_<custom>(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;

	/* Parameter validation */
	if (params[P_PARAM_NAME_1] < 0 || params[P_PARAM_NAME_1] > UINT8_MAX||
	    params[P_PARAM_NAME_1] < 0 || params[P_PARAM_NAME_1] > UINT8_MAX) {
		return -EINVAL;
	}

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_<CUSTOM>, sizeof(*op), img->fmt.pitch);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Store parameters */
	op->param_1 = params[P_PARAM_NAME_1];
	op->param_2 = params[P_PARAM_NAME_2];

	return 0;
}
```

## Define the runner function

This function will be called whenever one more input line is available and can be processed.

```c
void mpix_run_<custom>(struct mpix_base_op *base)
{
	/* Get the custom operation by casting it from the base operation (optional) */
	struct mpix_operation *op = (void *)base;

	/* Input and output buffers */
	const uint8_t *src;
	uint8_t *dst;

	/* Request one line of input */
	MPIX_OP_INPUT_LINES(base, &src, 1);

	/* Request one line of output */
	MPIX_OP_OUTPUT_LINE(base, &dst);

	/* Do some custom processing here */
	for (int i = 0; i < base->fmt.width; i++) {
		src[i] += op->param_1;
	}

	/* Confirm the output line is processed and available for the next operation */
	MPIX_OP_OUTPUT_DONE(base);

	/* Confirm that 1 input line is completely processed and is not needed anymore */
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
```

## Using the operation

The new custom operation can now be used in the application using the user API.

There is a generic way to call every operation: @ref mpix_pipeline_add.

Every language binding used will be updated to also feature the operation you are using as it is
registered globally.
