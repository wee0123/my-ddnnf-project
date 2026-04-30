use rug::{Assign, Integer};

use super::super::node::NodeType::*;
use crate::Ddnnf;

struct OmissionContext {
    changed: Vec<bool>,
    temp: Vec<Integer>,
    seen: Vec<bool>,
    touched: Vec<usize>,
}

impl OmissionContext {
    fn new(node_count: usize) -> OmissionContext {
        OmissionContext {
            changed: vec![false; node_count],
            temp: vec![Integer::ZERO; node_count],
            seen: vec![false; node_count],
            touched: Vec::new(),
        }
    }

    #[inline]
    fn set_zero(&mut self, index: usize) {
        self.changed[index] = true;
        self.temp[index].assign(Integer::ZERO);
    }
}

impl Ddnnf {
    #[inline]
    pub(crate) fn operate_on_partial_config_omitted(&mut self, features: &[i32]) -> Integer {
        if self.query_is_not_sat(features) {
            Integer::ZERO
        } else {
            let features: Vec<i32> = self.reduce_query(features);
            let indexes: Vec<usize> = self.map_features_opposing_indexes(&features);

            if indexes.is_empty() {
                return self.rc();
            }

            self.omit_from_zero_literals(&indexes)
        }
    }

    #[inline]
    fn omit_from_zero_literals(&self, zero_literal_indexes: &[usize]) -> Integer {
        let mut ctx = OmissionContext::new(self.nodes.len());

        for &index in zero_literal_indexes {
            ctx.set_zero(index);
            for parent in self.nodes[index].parents.clone() {
                self.collect_omission_ancestors(parent, &mut ctx);
            }
        }

        ctx.touched.sort_unstable();

        for i in ctx.touched.clone() {
            self.calc_omitted_node(i, &mut ctx);
        }

        let root = self.nodes.len() - 1;
        if ctx.changed[root] {
            ctx.temp[root].clone()
        } else {
            self.rc()
        }
    }

    #[inline]
    fn collect_omission_ancestors(&self, index: usize, ctx: &mut OmissionContext) {
        if ctx.seen[index] {
            return;
        }

        ctx.seen[index] = true;
        ctx.touched.push(index);

        for parent in self.nodes[index].parents.clone() {
            self.collect_omission_ancestors(parent, ctx);
        }
    }

    #[inline]
    fn calc_omitted_node(&self, index: usize, ctx: &mut OmissionContext) {
        match &self.nodes[index].ntype {
            And { children } => self.calc_omitted_and(index, children.clone(), ctx),
            Or { children } => self.calc_omitted_or(index, children.clone(), ctx),
            False => ctx.set_zero(index),
            _ => (),
        }
    }

    #[inline]
    fn calc_omitted_and(&self, index: usize, children: Vec<usize>, ctx: &mut OmissionContext) {
        let mut changed = false;
        let mut product = Integer::from(1);

        for child in children {
            if ctx.changed[child] {
                changed = true;
                if ctx.temp[child] == 0 {
                    ctx.set_zero(index);
                    return;
                }
                product *= &ctx.temp[child];
            } else {
                if self.nodes[child].count == 0 {
                    ctx.set_zero(index);
                    return;
                }
                product *= &self.nodes[child].count;
            }
        }

        if changed && product != self.nodes[index].count {
            ctx.changed[index] = true;
            ctx.temp[index] = product;
        } else {
            ctx.changed[index] = false;
        }
    }

    #[inline]
    fn calc_omitted_or(&self, index: usize, children: Vec<usize>, ctx: &mut OmissionContext) {
        let mut changed = false;
        let mut sum = self.nodes[index].count.clone();

        for child in children {
            if ctx.changed[child] {
                changed = true;
                sum -= &self.nodes[child].count;
                sum += &ctx.temp[child];
            }
        }

        if changed && sum != self.nodes[index].count {
            ctx.changed[index] = true;
            ctx.temp[index] = sum;
        } else {
            ctx.changed[index] = false;
        }
    }
}

#[cfg(test)]
mod test {
    use crate::{parser::build_ddnnf, Ddnnf};

    fn assert_same_as_marker(ddnnf: &mut Ddnnf, query: &[i32]) {
        let marker = ddnnf.operate_on_partial_config_marker(query, Ddnnf::calc_count_marked_node);
        let omitted = ddnnf.operate_on_partial_config_omitted(query);
        assert_eq!(marker, omitted, "query: {query:?}");
    }

    #[test]
    fn omitted_count_small_examples() {
        let mut ddnnf = build_ddnnf("tests/data/small_ex_c2d.nnf", None);

        let queries = [
            vec![1],
            vec![-1],
            vec![2],
            vec![2, 4],
            vec![1, 3, 4],
            vec![-1, -2, -3],
            vec![1, -1],
        ];

        for query in queries {
            assert_same_as_marker(&mut ddnnf, &query);
        }
    }

    #[test]
    fn omitted_count_matches_default_for_larger_queries() {
        let mut ddnnf = build_ddnnf("tests/data/VP9_d4.nnf", Some(42));

        let queries = [
            vec![1, 2, 3, 4, 5],
            vec![-1, 2, -3, 4, -5],
            vec![1, 4, 7, 10, 13, 16, 19, 22],
        ];

        for query in queries {
            let omitted = ddnnf.operate_on_partial_config_omitted(&query);
            let default = ddnnf.operate_on_partial_config_default(&query, Ddnnf::calc_count);
            assert_eq!(omitted, default, "query: {query:?}");
        }
    }
}
