- Decision: Bug
- Reason: This is a **true positive** relative to the target bug pattern.

  The target pattern is: an aggregate floating-point value is computed from runtime data and later used as a divisor without checking whether it is zero. That is exactly what happens here in `fn(ir_gain)`:

  ```c
  ftype ch_gain, sum = 0;
  ...
  } else if (s->ir_norm == 0.f) {
      for (int i = 0; i < cur_nb_taps; i++)
          sum += time[i];
      ch_gain = 1. / sum;
  }
  ```

  ## Control/data flow
  On the reported path:
  - `s->ir_norm == 0.f`
  - `sum` is initialized to `0`
  - the loop accumulates `sum += time[i]`
  - then `sum` is used as divisor in `1. / sum` with no zero check

  So `sum` is an aggregate factor derived from input samples, just like `norm_fac` in the reference bug.

  ## Feasibility
  The analyzer’s shown path assumes the loop does not execute (`i >= cur_nb_taps` immediately), which means `cur_nb_taps == 0` is one feasible case and leaves `sum == 0`. But even if `cur_nb_taps > 0`, zero is still feasible:
  - `time[]` may contain all zeros
  - or positive and negative values summing to zero

  From the surrounding code:
  - `cur_nb_taps = s->ir[selir]->nb_samples;`
  - `nb_taps` is checked earlier when initially deriving `s->nb_taps[selir]` (`<= 0` rejected), but `cur_nb_taps` comes from the consumed frame and is not shown to be revalidated here as strictly positive.
  - Regardless, a positive `cur_nb_taps` does **not** prevent `sum == 0`.

  Thus the divide-by-zero is realistically triggerable with valid runtime data.

  ## Match to target bug pattern
  This matches the reference pattern closely:
  - aggregate value computed from data (`sum += time[i]`)
  - later inverted/divided (`1. / sum`)
  - no guard for zero aggregate

  ## Relation to pre-/post-patch behavior
  The reference patch fixed:
  ```c
  norm_fac = norm_fac ? 1.0f / norm_fac : 0;
  ```
  A similar fix would address this report, e.g. guarding `sum` before `1. / sum`. So it has the same root cause shape and remediation style.

  Therefore this report is a real bug and matches the target bug pattern.