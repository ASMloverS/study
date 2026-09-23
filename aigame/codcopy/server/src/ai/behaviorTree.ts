export type Status = 'success' | 'failure' | 'running';

export interface BTNode<C> {
  tick(ctx: C): Status;
}

export function Selector<C>(...children: BTNode<C>[]): BTNode<C> {
  return {
    tick(ctx) {
      for (const child of children) {
        const s = child.tick(ctx);
        if (s !== 'failure') return s;
      }
      return 'failure';
    },
  };
}

export function Sequence<C>(...children: BTNode<C>[]): BTNode<C> {
  return {
    tick(ctx) {
      let last: Status = 'success';
      for (const child of children) {
        last = child.tick(ctx);
        if (last !== 'success') return last;
      }
      return last;
    },
  };
}

export function Cond<C>(fn: (ctx: C) => boolean): BTNode<C> {
  return {
    tick(ctx) {
      return fn(ctx) ? 'success' : 'failure';
    },
  };
}

export function Act<C>(fn: (ctx: C) => Status): BTNode<C> {
  return {
    tick(ctx) {
      return fn(ctx);
    },
  };
}
