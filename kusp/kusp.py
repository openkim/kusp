import inspect
import typing
from typing import Tuple, Union

import numpy as np
from loguru import logger
import sys


def _ensure_default_logging(default_level: str = "WARNING") -> None:
    """
    If loguru is still in default state (single handler id=0), replace
    the default DEBUG handler with a quieter one, mostly for C++ interface.
    Otherwise defaults to debug and C++ side becoms too noisy.

    Avoids overriding when logging is already configured.
    Only catches internal-structure-related exceptions, not interrupts.
    """
    try:
        handlers = logger._core.handlers  # internal access but stable enough
        if len(handlers) == 1:
            handler = next(iter(handlers.values()))
            handler_id = getattr(handler, "_id", None)

            if handler_id == 0:
                logger.remove()
                # logger.add(sys.stderr, level=default_level)
                # print("in c++")

    except (KeyError, AttributeError, TypeError):
        return


def kusp_model(
    influence_distance: Union[float, np.float64, np.ndarray],
    species: Tuple[str, ...],
    strict_arg_check: bool = True,
    **metadata,
):
    """Mark a callable as a KUSP model entry point.

    Args:
        influence_distance: Cutoff distance advertised to KIM-API.
        species: Tuple of species symbols in the order expected by the model.
        strict_arg_check: Whether to raise instead of warn on signature issues.
        **metadata: Extra attributes stored on the decorated object.

    TODO:
        Add support for units. It is easy, just extra decorator arguments.

    Returns:
        Decorating function that adds bookkeeping attributes.
    """
    _ensure_default_logging()

    def _decorator(functor):
        logger.debug(f"Received influence_distance: {influence_distance}, for object {functor}")
        if strict_arg_check:
            logger.debug("Using strict arg check; pass strict_arg_check=False to only warn.")

        # Pick target: function or class.__call__
        target = functor.__call__ if inspect.isclass(functor) else functor
        logger.debug(f"Got the functor: {target}")

        if target is not None:
            sig = inspect.signature(target)
            params = list(sig.parameters.values())
            user_params = params[1:] if inspect.isclass(functor) else params

            if len(user_params) < 3:
                msg = "Model must accept three parameters: (species, positions, contributing)."
                logger.error(msg)
                raise TypeError(msg)

            if any(
                p.kind in (inspect.Parameter.VAR_POSITIONAL, inspect.Parameter.VAR_KEYWORD)
                for p in user_params
            ):
                msg = "Do not use *args/**kwargs; expect exactly (species, positions, contributing)."
                logger.error(msg)
                raise TypeError(msg)

            try:
                hints = typing.get_type_hints(target)
            except Exception:
                hints = {}

            hints.pop("self", None)
            r = hints.get("return")
            if not (
                r is not None
                and typing.get_origin(r) in (tuple, Tuple)
                and len(typing.get_args(r)) == 2
                and typing.get_args(r)[0] is np.ndarray
                and typing.get_args(r)[1] is np.ndarray
            ):
                msg = "Missing/incorrect return type hint; expected Tuple[np.ndarray, np.ndarray]."
                msg += "Either provide concrete hints or pass strict_arg_check=False argument in decorator."
                logger.error(msg)
                raise TypeError(msg)

        # Annotate functor for KUSP
        functor.__kusp_model__ = True
        functor.__kusp_metadata__ = metadata
        functor.__kusp_influence_distance__ = influence_distance
        functor.__kusp_species__ = species
        logger.debug(f"All done, returning the object: {functor.__dict__}")
        return functor

    return _decorator
