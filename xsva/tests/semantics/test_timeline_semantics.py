"""Timeline semantics tests for the xsva MVP subset."""

from xsva.ir.diagnostics import DiagnosticBag
from xsva.lower.sequence_to_timeline import lower_sequence_to_timeline
from xsva.lower.surface_to_sequence import lower_surface_to_sequence
from xsva.parser.property_parser import PropertyParser
from xsva.parser.scanner import Scanner


def _timeline(source: str, name: str):
    diag = DiagnosticBag()
    parser = PropertyParser(Scanner(source, file="<test>"), diag)
    surfaces = parser.parse_file()
    surface = next(s for s in surfaces if s.name == name)
    seq = lower_surface_to_sequence(surface, diag)
    timeline = lower_sequence_to_timeline(seq, surface_ir=surface, diag=diag)
    return timeline


def _single_obligation(source: str, name: str):
    timeline = _timeline(source, name)
    assert timeline.lowering_status.value == "exact"
    assert len(timeline.obligations) == 1
    return timeline.obligations[0]


def test_overlapped_implication_cycle_zero():
    ob = _single_obligation("""
property p;
  req |-> ack;
endproperty
""", "p")
    assert ob.expr == "ack"
    assert ob.cycle == 0


def test_nonoverlapped_implication_cycle_one():
    ob = _single_obligation("""
property p;
  req |=> ack;
endproperty
""", "p")
    assert ob.expr == "ack"
    assert ob.cycle == 1


def test_fixed_delay_cycles_accumulate_after_implication():
    ob = _single_obligation("""
property p;
  req |-> ##2 ack;
endproperty
""", "p")
    assert ob.cycle == 2

    ob = _single_obligation("""
property p;
  req |=> ##2 ack;
endproperty
""", "p")
    assert ob.cycle == 3


def test_delay_range_becomes_eventually_window():
    ob = _single_obligation("""
property p;
  req |-> ##[1:4] ack;
endproperty
""", "p")
    assert ob.kind.value == "eventually"
    assert ob.window.start == 1
    assert ob.window.end == 4


def test_delay_range_suffix_expands_paths():
    timeline = _timeline("""
property p;
  req |-> ##[1:3] ack ##1 done;
endproperty
""", "p")
    assert timeline.lowering_status.value == "exact"
    cycles = [[ob.cycle for ob in path.obligations] for path in timeline.match_paths]
    assert cycles == [[1, 2], [2, 3], [3, 4]]


def test_local_capture_in_trigger_and_dependency():
    timeline = _timeline("""
property p;
  logic [31:0] v;
  @(posedge clk) disable iff (!rst_n)
  (req, v = data) |-> ##[1:4] ack && rsp == v;
endproperty
""", "p")
    assert timeline.clock.signal == "clk"
    assert timeline.disable_expr == "! rst_n"
    assert timeline.trigger.expr == "req"
    assert [(c.var, c.value_expr) for c in timeline.trigger.captures] == [("v", "data")]
    assert timeline.obligations[0].depends_on_captures == ["v"]


def test_first_match_is_partial_not_crash():
    timeline = _timeline("""
property p;
  req |-> first_match(##[1:4] ack) ##1 done;
endproperty
""", "p")
    assert timeline.lowering_status.value == "partial"
    assert any("first_match" in d.message for d in timeline.diagnostics)
