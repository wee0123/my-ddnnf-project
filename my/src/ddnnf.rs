pub mod anomalies;
pub mod clause_cache;
pub mod counting;
pub mod heuristics;
pub mod multiple_queries;
pub mod node;
pub mod stream;

use std::collections::{BTreeSet, HashMap, HashSet};

use itertools::Either;
use rug::{Assign, Integer};

use self::{
    clause_cache::ClauseCache,
    node::{Node, NodeType::*},
};

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

#[derive(Clone, Debug)]
/// A Ddnnf holds all the nodes as a vector, also includes metadata and further information that is used for optimations
pub struct Ddnnf {
    /// The actual nodes of the d-DNNF in postorder
    pub nodes: Vec<Node>,
    /// The saved state to enable undoing and adapting the d-DNNF. Avoid exposing this field outside of this source file!
    cached_state: Option<ClauseCache>,
    /// Literals for upwards propagation
    pub literals: HashMap<i32, usize>, // <var_number of the Literal, and the corresponding indize>
    true_nodes: Vec<usize>, // Indices of true nodes. In some cases those nodes needed to have special treatment
    /// The core/dead features of the model corresponding with this ddnnf
    pub core: HashSet<i32>,
    /// An interim save for the marking algorithm
    pub md: Vec<usize>,
    pub number_of_variables: u32,
    /// The number of threads
    pub max_worker: u16,
}

impl Default for Ddnnf {
    fn default() -> Self {
        Ddnnf {
            nodes: Vec::new(),
            cached_state: None,
            literals: HashMap::new(),
            true_nodes: Vec::new(),
            core: HashSet::new(),
            md: Vec::new(),
            number_of_variables: 0,
            max_worker: 4,
        }
    }
}

impl Ddnnf {
    /// Creates a new ddnnf including dead and core features
    pub fn new(
        nodes: Vec<Node>,
        literals: HashMap<i32, usize>,
        true_nodes: Vec<usize>,
        number_of_variables: u32,
        clauses: Option<BTreeSet<BTreeSet<i32>>>,
    ) -> Ddnnf {
        let mut ddnnf = Ddnnf {
            nodes,
            cached_state: None,
            literals,
            true_nodes,
            core: HashSet::new(),
            md: Vec::new(),
            number_of_variables,
            max_worker: 4,
        };
        ddnnf.get_core();
        if let Some(c) = clauses {
            ddnnf.update_cached_state(Either::Right(c), Some(number_of_variables));
        }
        ddnnf
    }

    /// Checks if the creation of a cached state is valid.
    /// That is only the case if the input format was CNF.
    pub fn can_save_state(&self) -> bool {
        self.cached_state.is_some()
    }

    /// Either initialises the ClauseCache by saving the clauses and its corresponding clauses
    /// or updates the state accordingly.
    pub fn update_cached_state(
        &mut self,
        clause_info: Either<(Vec<BTreeSet<i32>>, Vec<BTreeSet<i32>>), BTreeSet<BTreeSet<i32>>>, // Left(edit operation) or Right(clauses)
        total_features: Option<u32>,
    ) -> bool {
        match self.cached_state.as_mut() {
            Some(state) => match clause_info.left() {
                Some((add, rmv)) => {
                    if total_features.is_none()
                        || !state.apply_edits_and_replace(add, rmv, total_features.unwrap())
                    {
                        return false;
                    }
                    // The old d-DNNF got replaced by the new one.
                    // Consequently, the current higher level d-DNNF becomes the older one.
                    // We swap their field data to keep the order without needing to deal with recursivly building up
                    // obselete d-DNNFs that trash the RAM.
                    self.swap();
                }
                None => return false,
            },
            None => match clause_info.right() {
                Some(clauses) => {
                    let mut state = ClauseCache::default();
                    state.initialize(clauses, total_features.unwrap());
                    self.cached_state = Some(state);
                }
                None => return false,
            },
        }
        true
    }

    fn swap(&mut self) {
        if let Some(cached_state) = self.cached_state.as_mut() {
            if let Some(save_state) = cached_state.old_state.as_mut() {
                std::mem::swap(&mut self.nodes, &mut save_state.nodes);
                std::mem::swap(&mut self.literals, &mut save_state.literals);
                std::mem::swap(&mut self.true_nodes, &mut save_state.true_nodes);
                std::mem::swap(&mut self.core, &mut save_state.core);
                std::mem::swap(&mut self.md, &mut save_state.md);
                std::mem::swap(
                    &mut self.number_of_variables,
                    &mut save_state.number_of_variables,
                );
                std::mem::swap(&mut self.max_worker, &mut save_state.max_worker);
            }
        }
    }

    // Performes an undo operation resulting in swaping the current d-DNNF with its older version.
    // Hence, the perviously older version becomes the current one and the current one becomes the older version.
    // A second undo operation in a row is equivalent to a redo. Can fail if there is no old d-DNNF available.
    pub fn undo_on_cached_state(&mut self) -> bool {
        match self.cached_state.as_mut() {
            Some(state) => {
                state.setup_for_undo();
                self.swap();
                //std::mem::swap(self, &mut state.to_owned().get_old_state().unwrap());
                true
            }
            None => false,
        }
    }

    // Returns the current count of the root node in the ddnnf.
    // That value is the same during all computations
    pub fn rc(&self) -> Integer {
        self.nodes[self.nodes.len() - 1].count.clone()
    }

    // Returns the current temp count of the root node in the ddnnf.
    // That value is changed during computations
    fn rt(&self) -> Integer {
        self.nodes[self.nodes.len() - 1].temp.clone()
    }

    /// Determines the positions of the inverted featueres
    pub fn map_features_opposing_indexes(&self, features: &[i32]) -> Vec<usize> {
        let mut indexes = Vec::with_capacity(features.len());
        for number in features {
            if let Some(i) = self.literals.get(&-number).cloned() {
                indexes.push(i);
            }
        }
        indexes
    }

    /// Executes a query.
    /// We use the in our opinion best type of query depending on the amount of features.
    ///
    /// # Example
    /// ```
    /// extern crate ddnnf_lib;
    /// use ddnnf_lib::Ddnnf;
    /// use ddnnf_lib::parser::*;
    /// use rug::Integer;
    ///
    /// // create a ddnnf
    /// let file_path = "./tests/data/small_ex_c2d.nnf";
    /// let mut ddnnf: Ddnnf = build_ddnnf(file_path, None);
    ///
    /// assert_eq!(1, ddnnf.execute_query(&vec![3,4]));
    /// assert_eq!(2, ddnnf.execute_query(&vec![3]));
    pub fn execute_query(&mut self, features: &[i32]) -> Integer {
        match features.len() {
            0 => self.rc(),
            1 => self.card_of_feature_with_marker(features[0]),
            2..=20 => self.operate_on_partial_config_omitted(features),
            _ => self.operate_on_partial_config_default(features, Ddnnf::calc_count),
        }
    }

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
    use crate::parser::build_ddnnf;

    #[test]
    fn features_opposing_indexes() {
        let ddnnf = build_ddnnf("tests/data/small_ex_c2d.nnf", None);

        assert_eq!(
            vec![3, 2, 6],
            ddnnf.map_features_opposing_indexes(&[1, 2, 3, 4])
        );
        assert_eq!(
            vec![0, 1, 4, 5],
            ddnnf.map_features_opposing_indexes(&[-1, -2, -3, -4])
        );
    }
}
