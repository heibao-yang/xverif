class XbitError(Exception):
    """Base error with a stable JSON error code."""

    code = "XBIT_ERROR"

    def __init__(self, message: str, **details):
        super().__init__(message)
        self.message = message
        self.details = {k: v for k, v in details.items() if v is not None}

    def to_error(self) -> dict:
        error = {"code": self.code, "message": self.message}
        if self.details:
            error["details"] = self.details
        return error


class ParseError(XbitError):
    code = "PARSE_ERROR"


class WidthError(XbitError):
    code = "WIDTH_OUT_OF_RANGE"


class ValueError2State(XbitError):
    code = "FOUR_STATE_LITERAL"


class FourStateUnsupported(XbitError):
    code = "FOUR_STATE_UNSUPPORTED"


class UnknownVariable(XbitError):
    code = "UNKNOWN_VARIABLE"


class EvalError(XbitError):
    code = "EVAL_ERROR"


class DivisionByZero(XbitError):
    code = "DIVISION_BY_ZERO"
