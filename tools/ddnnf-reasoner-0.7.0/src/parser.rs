pub mod c2d_lexer;
use c2d_lexer::{lex_line_c2d, C2DToken, TId};

pub mod d4_lexer;
use d4_lexer::{lex_line_d4, D4Token};

pub mod from_cnf;
use from_cnf::{check_for_cnf_header, CNFToken};

pub mod persisting;
pub mod util;

use core::panic;
use std::{
    cell::RefCell,
    cmp::max,
    collections::{BTreeSet, HashMap, HashSet},
    ffi::OsStr,
    fs::{self, File},
    io::{BufRead, BufReader},
    path::Path,
    process,
    rc::Rc,
};

use rug::{Complete, Integer};

use crate::ddnnf::{node::Node, node::NodeType, Ddnnf};

use crate::d4_lexer::D4Token::{And, False, Or, True};
use petgraph::visit::{Dfs, IntoNeighborsDirected};
use petgraph::{
    graph::{EdgeIndex, NodeIndex},
    stable_graph::StableGraph,
    visit::DfsPostOrder,
    Direction::{Incoming, Outgoing},
};

use tempfile::tempdir;

///new entry
#[inline]
pub fn build_ddnnf(path: &str, total_features: Option<u32>) -> Ddnnf {
    if let Some(extension) = Path::new(path).extension().and_then(OsStr::to_str) {
        if extension == "dimacs" || extension == "cnf" {
            let (inferred_total, clauses) = parse_dimacs_info(path);
            let effective_total_features = total_features.or(inferred_total);

            let dir = tempdir().unwrap();
            let out_path = dir.path().join("tmp_output.nnf");
            let out_str = out_path.to_str().unwrap();

            run_d4_to_file(path, out_str);

            let file = open_file_savely(out_str);
            let lines = BufReader::new(file)
                .lines()
                .map(|line| line.expect("Unable to read line"))
                .collect::<Vec<String>>();

            return distribute_building(lines, effective_total_features, Some(clauses));
        }
    }

    let file = open_file_savely(path);
    let lines = BufReader::new(file)
        .lines()
        .map(|line| line.expect("Unable to read line"))
        .collect::<Vec<String>>();

    distribute_building(lines, total_features, None)
}

///d4
#[inline]
pub fn distribute_building(
    lines: Vec<String>,
    total_features: Option<u32>,
    clauses: Option<BTreeSet<BTreeSet<i32>>>,
) -> Ddnnf {
    use C2DToken::*;

    match lex_line_c2d(lines[0].trim()) {
        Ok((
               _,
               Header {
                   nodes: _,
                   edges: _,
                   variables,
               },
           )) => build_c2d_ddnnf(lines, variables as u32, clauses),
        Ok(_) | Err(_) => match total_features {
            Some(o) => build_d4_ddnnf(lines, Some(o), clauses),
            None => {
                println!(
                    "\x1b[1;38;5;226mWARNING: The first line of the file isn't a c2d header and the option 'total_features' is not set. \
                        Hence, we can't determine the number of variables and as a result, we might not be able to construct a valid ddnnf. \
                        Nonetheless, we build a ddnnf with our limited information.\n\x1b[0m"
                );
                build_d4_ddnnf(lines, None, clauses)
            }
        },
    }
}

use std::process::Command;

fn run_d4_to_file(input_cnf: &str, output_nnf: &str) {
    let output = Command::new("d4")
        .arg("-dDNNF")
        .arg(input_cnf)
        .arg(format!("-out={}", output_nnf))
        .output()
        .unwrap_or_else(|e| {
            panic!("Failed to execute d4: {}", e);
        });

    if !output.status.success() {
        panic!(
            "d4 failed.\nstdout:\n{}\nstderr:\n{}",
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        );
    }
}

fn parse_dimacs_info(path: &str) -> (Option<u32>, BTreeSet<BTreeSet<i32>>) {
    let file = File::open(path)
        .unwrap_or_else(|e| panic!("Failed to open CNF file \"{}\": {}", path, e));

    let reader = BufReader::new(file);

    let mut total_features: Option<u32> = None;
    let mut clauses: BTreeSet<BTreeSet<i32>> = BTreeSet::new();
    let mut current_clause: BTreeSet<i32> = BTreeSet::new();

    for line in reader.lines() {
        let line = line.unwrap_or_else(|e| {
            panic!("Failed to read line from CNF file \"{}\": {}", path, e)
        });

        let line = line.trim();

        if line.is_empty() || line.starts_with('c') {
            continue;
        }

        if line.starts_with('p') {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() < 4 || parts[0] != "p" || parts[1] != "cnf" {
                panic!("Invalid CNF header in \"{}\": {}", path, line);
            }

            total_features = Some(parts[2].parse::<u32>().unwrap_or_else(|e| {
                panic!(
                    "Failed to parse variable count from CNF header in \"{}\": {} ({})",
                    path, line, e
                )
            }));

            continue;
        }

        for tok in line.split_whitespace() {
            let lit = tok.parse::<i32>().unwrap_or_else(|e| {
                panic!("Invalid literal \"{}\" in CNF file \"{}\": {}", tok, path, e)
            });

            if lit == 0 {
                if !current_clause.is_empty() {
                    clauses.insert(std::mem::take(&mut current_clause));
                }
            } else {
                current_clause.insert(lit);
            }
        }
    }

    if !current_clause.is_empty() {
        panic!(
            "CNF file \"{}\" ended before clause terminator 0 was found for the last clause",
            path
        );
    }

    (total_features, clauses)
}

/// Parses a ddnnf, referenced by the file path.
/// This function uses C2DTokens which specify a d-DNNF in c2d format.
/// The file gets parsed and we create the corresponding data structure.
#[inline]
fn build_c2d_ddnnf(
    lines: Vec<String>,
    variables: u32,
    clauses: Option<BTreeSet<BTreeSet<i32>>>,
) -> Ddnnf {
    use C2DToken::*;

    let mut parsed_nodes: Vec<Node> = Vec::with_capacity(lines.len());

    let mut literals: HashMap<i32, usize> = HashMap::new();
    let mut true_nodes = Vec::new();

    // opens the file with a BufReader and
    // works off each line of the file data seperatly
    // skip the first line, because we already looked at the header
    for line in lines.into_iter().skip(1) {
        let next: Node = match lex_line_c2d(line.as_ref()).unwrap().1 {
            And { children } => {
                Node::new_and(calc_and_count(&mut parsed_nodes, &children), children)
            }
            Or { decision, children } => Node::new_or(
                decision,
                calc_or_count(&mut parsed_nodes, &children),
                children,
            ),
            Literal { feature } => Node::new_literal(feature),
            True => Node::new_bool(true),
            False => Node::new_bool(false),
            _ => panic!("Tried to parse the header of the .nnf at the wrong time"),
        };

        // fill the parent node pointer, save literals
        match &next.ntype {
            NodeType::And { children } | NodeType::Or { children } => {
                let next_indize: usize = parsed_nodes.len();
                for &i in children {
                    parsed_nodes[i].parents.push(next_indize);
                }
            }
            // fill the FxHashMap with the literals
            NodeType::Literal { literal } => {
                literals.insert(*literal, parsed_nodes.len());
            }
            NodeType::True => {
                true_nodes.push(parsed_nodes.len());
            }
            _ => (),
        }

        parsed_nodes.push(next);
    }

    Ddnnf::new(parsed_nodes, literals, true_nodes, variables, clauses)
}

fn build_d4_ddnnf_opt(
    lines: Vec<String>,
    total_features_opt: Option<u32>,
    clauses: Option<BTreeSet<BTreeSet<i32>>>,
) -> Ddnnf {
    //build D4BuildContext
    let mut ctx = D4BuildContext::new();
    ctx.total_features = total_features_opt.unwrap_or(0);
    ctx.literal_occurrences = vec![false; max(100_000, ctx.total_features as usize)];
    ctx.or_triangles = vec![None; (ctx.total_features + 1) as usize];

    parse_d4_into_graph(&mut ctx, lines);
    attach_synthetic_root_and_missing_vars(&mut ctx);
    simplify_constants(&mut ctx);
    smooth_or_nodes(&mut ctx);
    let dbi = lower_graph_to_nodes(&mut ctx);

    Ddnnf::new(
        dbi.parsed_nodes,
        dbi.literals,
        dbi.true_nodes,
        ctx.total_features,
        clauses,
    )
}

struct D4BuildContext {
    graph: StableGraph<TId, ()>,
    total_features: u32,
    literal_occurrences: Vec<bool>,
    indices: Vec<NodeIndex>,
    nx_literals: HashMap<NodeIndex, i32>,
    literals_nx: HashMap<i32, NodeIndex>,
    or_triangles: Vec<Option<NodeIndex>>,
    root: Option<NodeIndex>,
}

struct DdnnfBuildInfo {
    parsed_nodes: Vec<Node>,
    literals: HashMap<i32, usize>,
    true_nodes: Vec<usize>,
}

type InfoCache = HashMap<NodeIndex, NodeInfo>;

impl DdnnfBuildInfo {
    fn new(capacity: usize) -> DdnnfBuildInfo {
        DdnnfBuildInfo {
            parsed_nodes: Vec::with_capacity(capacity),
            literals: HashMap::new(),
            true_nodes: Vec::new(),
        }
    }
}

impl D4BuildContext {
    fn new() -> D4BuildContext {
        D4BuildContext {
            graph: StableGraph::<TId, ()>::new(),
            total_features: 0,
            literal_occurrences: Vec::<bool>::new(),
            indices: Vec::<NodeIndex>::new(),
            nx_literals: HashMap::<NodeIndex, i32>::new(),
            literals_nx: HashMap::<i32, NodeIndex>::new(),
            or_triangles: Vec::<Option<NodeIndex>>::new(),
            root: None,
        }
    }

    fn ensure_literal_node(&mut self, literal: i32) -> NodeIndex {
        if let Some(&nx) = self.literals_nx.get(&literal) {
            return nx;
        }

        let nx = if literal.is_positive() {
            self.graph.add_node(TId::PositiveLiteral)
        } else {
            self.graph.add_node(TId::NegativeLiteral)
        };

        self.nx_literals.insert(nx, literal);
        self.literals_nx.insert(literal, nx);
        nx
    }

    fn ensure_literal_nodes(&mut self, literals: &[i32]) -> Vec<NodeIndex> {
        literals
            .iter()
            .map(|&lit| self.ensure_literal_node(lit))
            .collect()
    }

    fn ensure_or_triangles(&mut self, feature: u32) -> NodeIndex {
        if let Some(nx) = self.or_triangles[feature as usize] {
            return nx;
        }
        let f = feature as i32;
        let or = self.graph.add_node(TId::Or);

        let pos_lit = self.ensure_literal_node(f);
        let neg_lit = self.ensure_literal_node(-f);

        self.graph.add_edge(or, pos_lit, ());
        self.graph.add_edge(or, neg_lit, ());
        self.or_triangles[feature as usize] = Some(or);
        or
    }

    fn attach_feature(&mut self, feature: u32, attach: NodeIndex) {
        let or_nx = self.ensure_or_triangles(feature);
        self.graph.add_edge(attach, or_nx, ());
    }
}

//解析d4
fn parse_d4_into_graph(ctx: &mut D4BuildContext, lines: Vec<String>) {
    for line in lines {
        let next: D4Token = lex_line_d4(line.as_ref()).unwrap().1;

        use D4Token::*;
        match next {
            Edge { from, to, features } => {
                for f in &features {
                    ctx.literal_occurrences[f.unsigned_abs() as usize] = true;
                    ctx.total_features = max(ctx.total_features, f.unsigned_abs());
                }
                let from_n = ctx.indices[from as usize - 1];
                let to_n = ctx.indices[to as usize - 1];
                let edge = ctx.graph.add_edge(from_n, to_n, ());
                resolve_weighted_edges(ctx, from_n, to_n, edge, features);
            }
            And => ctx.indices.push(ctx.graph.add_node(TId::And)),
            Or => ctx.indices.push(ctx.graph.add_node(TId::Or)),
            True => ctx.indices.push(ctx.graph.add_node(TId::True)),
            False => ctx.indices.push(ctx.graph.add_node(TId::False)),
        }
    }
}

//去除带权边
fn resolve_weighted_edges(
    ctx: &mut D4BuildContext,
    from: NodeIndex,
    to: NodeIndex,
    edge: EdgeIndex,
    weights: Vec<i32>,
) {
    if weights.is_empty() {
        return;
    }
    let and_node = ctx.graph.add_node(TId::And);
    let literal_nodes = ctx.ensure_literal_nodes(weights.as_slice());

    ctx.graph.remove_edge(edge);

    ctx.graph.add_edge(from, and_node, ());
    for node in literal_nodes {
        ctx.graph.add_edge(and_node, node, ());
    }
    ctx.graph.add_edge(and_node, to, ());
}

//设置根结点+补全缺少变量（全局）
fn attach_synthetic_root_and_missing_vars(ctx: &mut D4BuildContext) {
    // add a new root which hold the unmentioned variables within the total_features range
    ctx.root = Option::from(ctx.graph.add_node(TId::And));
    ctx.graph.add_edge(ctx.root.unwrap(), NodeIndex::new(0), ());

    // add literals that are not mentioned in the ddnnf to the new root node
    for i in 1..=ctx.total_features {
        if !ctx.literal_occurrences[i as usize] {
            ctx.attach_feature(i, ctx.root.unwrap());
        }
    }
}

//化简
fn simplify_constants(ctx: &mut D4BuildContext) {
    loop {
        let mut dfs = DfsPostOrder::new(&ctx.graph, ctx.root.unwrap());
        let mut nodes = Vec::new();
        let all_nodes:Vec<NodeIndex> = ctx.graph.node_indices().collect();

        while let Some(nx) = dfs.next(&ctx.graph) {
            nodes.push(nx);
        }

        let reachable:HashSet<NodeIndex> = nodes.iter().copied().collect();
        for node in all_nodes {
            if ctx.graph.contains_node(node) && !reachable.contains(&node) {
                ctx.graph.remove_node(node);
            }
        }

        let mut cache = InfoCache::new();

        let all_changed = simplify_with_rules(&mut ctx.graph, nodes, &mut cache, ctx.root);

        if !all_changed {
            break;
        }
    }
}

fn simplify_with_rules(graph: &mut StableGraph<TId, ()>, nodes: Vec<NodeIndex>, cache: &mut InfoCache, root: Option<NodeIndex>) -> bool {
    let mut all_changed = false;
    for nx in nodes {
        if !graph.contains_node(nx) {
            continue;
        }

        let mut changed = false;

        changed |= remove_true_from_and(graph, nx);
        changed |= remove_false_from_or(graph, nx);
        changed |= remove_and_with_false(graph, nx, root);
        changed |= remove_or_with_true(graph, nx, root);

        if graph.contains_node(nx) {
            changed |= flatten_same_type(graph, nx);
        }
        if graph.contains_node(nx) {
            changed |= remove_duplicate_children(graph, nx);
        }
        if graph.contains_node(nx) {
            changed |= flatten_single_child(graph, nx, root);
        }

        if changed {
            if graph.contains_node(nx) {
                invalidate_upward(graph, cache, nx);
            }
            all_changed = true;
        }
    }

    all_changed
}

// And(true, a)->And(a)
fn remove_true_from_and(graph: &mut StableGraph<TId, ()>, node: NodeIndex) -> bool {
    if graph[node] != TId::And {
        return false;
    }

    let children: Vec<_> = graph.neighbors_directed(node, Outgoing).collect();
    let mut changed = false;
    for child in children {
        if graph.contains_node(child) && graph[child] == TId::True {
            if let Some(e) = graph.find_edge(node, child) {
                graph.remove_edge(e).unwrap();
                changed = true;
            }
        }
    }

    changed
}

//Or(false, a)->Or(a)
fn remove_false_from_or(graph: &mut StableGraph<TId, ()>, node: NodeIndex) -> bool {
    if graph[node] != TId::Or {
        return false;
    }

    let children: Vec<_> = graph.neighbors_directed(node, Outgoing).collect();
    let mut changed = false;
    for child in children {
        if graph.contains_node(child) && graph[child] == TId::False {
            if let Some(e) = graph.find_edge(node, child) {
                graph.remove_edge(e).unwrap();
                changed = true;
            }
        }
    }

    changed
}

//And(false, a)->false
fn remove_and_with_false(graph: &mut StableGraph<TId, ()>, node: NodeIndex, root: Option<NodeIndex>) -> bool {
    if graph[node] != TId::False {
        return false;
    }

    let mut to_remove = Vec::new();
    let mut visited = HashSet::new();
    let mut stack = vec![node];

    while let Some(current) = stack.pop() {
        for parent in graph.neighbors_directed(current, Incoming) {
            if !visited.insert(parent) {
                continue;
            }
            if Some(parent) == root {
                continue;
            }
            if graph[parent] == TId::And {
                to_remove.push(parent);
                stack.push(parent); // 继续向上遍历该父节点的父节点
            }
            // 如果父节点不是 And，则停止该分支（不继续向上）
        }
    }

    let mut changed = false;
    for node_to_del in to_remove {
        if graph.contains_node(node_to_del) {
            graph.remove_node(node_to_del);
            changed = true;
        }
    }
    changed
}

//Or(true, a)->true
fn remove_or_with_true(graph: &mut StableGraph<TId, ()>, node: NodeIndex, root: Option<NodeIndex>) -> bool {
    if graph[node] != TId::True {
        return false;
    }

    let mut to_remove = Vec::new();
    let mut visited = HashSet::new();
    let mut stack = vec![node];

    while let Some(current) = stack.pop() {
        for parent in graph.neighbors_directed(current, Incoming) {
            if !visited.insert(parent) {
                continue;
            }
            if Some(parent) == root {
                continue;
            }
            if graph[parent] == TId::Or {
                to_remove.push(parent);
                stack.push(parent);
            }
        }
    }

    let mut changed = false;
    for node_to_del in to_remove {
        if graph.contains_node(node_to_del) {
            graph.remove_node(node_to_del);
            changed = true;
        }
    }
    changed
}

//And(a)->a; Or(a)->a
fn flatten_single_child(graph: &mut StableGraph<TId, ()>, node: NodeIndex, root: Option<NodeIndex>) -> bool {
    let children: Vec<_> = graph.neighbors_directed(node, Outgoing).collect();
    if children.len() != 1 || Some(node) == root {
        return false;
    }

    let child = children[0];
    let parents: Vec<_> = graph.neighbors_directed(node, Incoming).collect();
    for parent in parents {
        if let Some(e) = graph.find_edge(parent, node) {
            graph.remove_edge(e).unwrap();
        }
        graph.add_edge(parent, child, ());
    }
    graph.remove_node(node);
    true
}

//And(a, And(b, c))->And(a, b, c)
fn flatten_same_type(graph: &mut StableGraph<TId, ()>, node: NodeIndex) -> bool {
    if graph[node] != TId::And && graph[node] != TId::Or {
        return false;
    }

    let mut changed = false;
    let children: Vec<_> = graph.neighbors_directed(node, Outgoing).collect();
    for child in children {
        if graph[child] == graph[node] {
            let parents: Vec<_> = graph.neighbors_directed(child, Incoming).collect();
            if parents.len() != 1 {
                continue;
            }

            let grandchildren: Vec<_> = graph.neighbors_directed(child, Outgoing).collect();
            if let Some(e) = graph.find_edge(node, child) {
                graph.remove_edge(e).unwrap();
            }

            for grandchild in grandchildren {
                if let Some(e) = graph.find_edge(child, grandchild) {
                    graph.add_edge(node, grandchild, ());
                }
            }

            graph.remove_node(child);
            changed = true;
        }
    }
    changed |= remove_duplicate_children(graph, node);

    changed
}

//And(a, a, b)->And(a, b)
fn remove_duplicate_children(graph: &mut StableGraph<TId, ()>, node: NodeIndex) -> bool {
    if graph[node] != TId::Or && graph[node] != TId::And {
        return false;
    }

    let mut changed = false;
    let children: Vec<_> = graph.neighbors_directed(node, Outgoing).collect();
    let mut seen = HashSet::new();
    for child in children {
        if !seen.insert(child) {
            if let Some(e) = graph.find_edge(node, child) {
                graph.remove_edge(e);
                changed = true;
            }
        }
    }

    changed
}

#[derive(Clone, Debug, Default)]
struct NodeInfo {
    vars: HashSet<u32>, 
    has_true: bool,
    has_false: bool,
}

fn collect_vars(
    graph: &StableGraph<TId, ()>,
    nx_literals: &HashMap<NodeIndex, i32>,
    cache: &mut InfoCache,
    node: NodeIndex,
) -> HashSet<u32> {
    if let Some(info) = cache.get(&node) {
        return info.vars.clone();
    }

    let mut vars = HashSet::new();
    match graph[node] {
        TId::PositiveLiteral | TId::NegativeLiteral => {
            vars.insert(nx_literals[&node].unsigned_abs());
        }
        TId::And | TId::Or => {
            for ch in graph.neighbors_directed(node, Outgoing) {
                vars.extend(collect_vars(graph, nx_literals, cache, ch));
            }
        }
        TId::True | TId::False => {}
        TId::Header => unreachable!(),
    }

    cache.insert(
        node,
        NodeInfo {
            vars: vars.clone(),
            has_true: graph[node] == TId::True,
            has_false: graph[node] == TId::False,
        },
    );
    vars
}

fn invalidate_upward(graph: &StableGraph<TId, ()>, cache: &mut InfoCache, start: NodeIndex) {
    let mut stack = vec![start];
    let mut seen = HashSet::new();

    while let Some(nx) = stack.pop() {
        if !seen.insert(nx) {
            continue;
        }
        cache.remove(&nx);
        for p in graph.neighbors_directed(nx, Incoming) {
            stack.push(p);
        }
    }
}

fn check_and_decomposable(
    ctx: &D4BuildContext,
    cache: &mut InfoCache,
    and_node: NodeIndex,
) -> bool {
    let children: Vec<_> = ctx.graph.neighbors_directed(and_node, Outgoing).collect();

    for i in 0..children.len() {
        let vi = collect_vars(&ctx.graph, &ctx.nx_literals, cache, children[i]);
        for j in i + 1..children.len() {
            let vj = collect_vars(&ctx.graph, &ctx.nx_literals, cache, children[j]);
            if !vi.is_disjoint(&vj) {
                return false;
            }
        }
    }
    true
}

fn check_or_smooth(ctx: &D4BuildContext, cache: &mut InfoCache, or_node: NodeIndex) -> bool {
    let children: Vec<_> = ctx.graph.neighbors_directed(or_node, Outgoing).collect();
    if children.len() <= 1 {
        return true;
    }

    let base = collect_vars(&ctx.graph, &ctx.nx_literals, cache, children[0]);
    for &ch in &children[1..] {
        let vars = collect_vars(&ctx.graph, &ctx.nx_literals, cache, ch);
        if vars != base {
            return false;
        }
    }
    true
}

//平滑化处理
fn smooth_or_nodes(ctx: &mut D4BuildContext) {
    let mut safe: HashMap<NodeIndex, HashSet<u32>> = HashMap::new();
    let mut dfs = DfsPostOrder::new(&ctx.graph, ctx.root.unwrap());

    while let Some(nx) = dfs.next(&ctx.graph) {
        // edges between going from an and node to another node do not
        // have any weights attached to them. Therefore, we can skip them
        if ctx.graph[nx] == TId::Or {
            let differences = get_literal_diff(&ctx.graph, &mut safe, &ctx.nx_literals, nx);
            // balance_or_children(&mut ctx.graph, nx, differences);
            for child in differences {
                let and_node = ctx.graph.add_node(TId::And);

                // place the newly created and node between the or node and its child
                ctx.graph
                    .remove_edge(ctx.graph.find_edge(nx, child.0).unwrap());
                ctx.graph.add_edge(nx, and_node, ());
                ctx.graph.add_edge(and_node, child.0, ());

                for literal in child.1 {
                    ctx.attach_feature(literal, and_node);
                }
            }
        }
    }
}

//压制
fn lower_graph_to_nodes(ctx: &mut D4BuildContext) -> DdnnfBuildInfo {
    let mut dfs = DfsPostOrder::new(&ctx.graph, ctx.root.unwrap());
    let mut nd_to_usize: HashMap<NodeIndex, usize> = HashMap::new();

    let mut dbi = DdnnfBuildInfo::new(ctx.graph.node_count());
    let nx_lit = &ctx.nx_literals;

    while let Some(nx) = dfs.next(&ctx.graph) {
        nd_to_usize.insert(nx, dbi.parsed_nodes.len());
        let neighs = ctx
            .graph
            .neighbors(nx)
            .map(|n| *nd_to_usize.get(&n).unwrap())
            .collect::<Vec<usize>>();
        let next: Node = match ctx.graph[nx] {
            // extract the parsed Token
            TId::PositiveLiteral | TId::NegativeLiteral => {
                Node::new_literal(nx_lit.get(&nx).unwrap().to_owned())
            }
            TId::And => Node::new_and(calc_and_count(&mut dbi.parsed_nodes, &neighs), neighs),

            TId::Or => Node::new_or(0, calc_or_count(&mut dbi.parsed_nodes, &neighs), neighs),
            TId::True => Node::new_bool(true),
            TId::False => Node::new_bool(false),
            TId::Header => panic!("The d4 standard does not include a header!"),
        };

        match &next.ntype {
            NodeType::And { children } | NodeType::Or { children } => {
                let next_indize: usize = dbi.parsed_nodes.len();
                for &i in children {
                    dbi.parsed_nodes[i].parents.push(next_indize);
                }
            }
            // fill the FxHashMap with the literals
            NodeType::Literal { literal } => {
                dbi.literals.insert(*literal, dbi.parsed_nodes.len());
            }
            NodeType::True => {
                dbi.true_nodes.push(dbi.parsed_nodes.len());
            }
            _ => (),
        }

        dbi.parsed_nodes.push(next);
    }

    dbi
}

/// Parses a ddnnf, referenced by the file path.
/// This function uses D4Tokens which specify a d-DNNF in d4 format.
/// The file gets parsed and we create the corresponding data structure.
#[inline]
fn build_d4_ddnnf(
    lines: Vec<String>,
    total_features_opt: Option<u32>,
    clauses: Option<BTreeSet<BTreeSet<i32>>>,
) -> Ddnnf {
    let mut ddnnf_graph = StableGraph::<TId, ()>::new(); //graph

    let mut total_features = total_features_opt.unwrap_or(0); //total_features
    let literal_occurences: Rc<RefCell<Vec<bool>>> =                                                //literal_occurences
        Rc::new(RefCell::new(vec![
            false;
            max(100_000, total_features as usize)
        ]));

    let mut indices: Vec<NodeIndex> = Vec::new(); //indices

    // With the help of the literals node state, we can add the required nodes
    // for the balancing of the or nodes to archieve smoothness
    let nx_literals: Rc<RefCell<HashMap<NodeIndex, i32>>> = Rc::new(RefCell::new(HashMap::new())); //nx_literals
    let literals_nx: Rc<RefCell<HashMap<i32, NodeIndex>>> = Rc::new(RefCell::new(HashMap::new())); //literals_nx

    let get_literal_indices =
        |ddnnf_graph: &mut StableGraph<TId, ()>, literals: Vec<i32>| -> Vec<NodeIndex> {
            let mut nx_lit = nx_literals.borrow_mut();
            let mut lit_nx = literals_nx.borrow_mut();

            let mut literal_nodes = Vec::new();

            for literal in literals {
                if literal.is_positive() {
                    literal_nodes.push(match lit_nx.get(&literal) {
                        Some(x) => *x,
                        None => {
                            let nx = ddnnf_graph.add_node(TId::PositiveLiteral);
                            nx_lit.insert(nx, literal);
                            lit_nx.insert(literal, nx);
                            nx
                        }
                    })
                } else {
                    literal_nodes.push(match lit_nx.get(&literal) {
                        Some(x) => *x,
                        None => {
                            let nx = ddnnf_graph.add_node(TId::NegativeLiteral);
                            nx_lit.insert(nx, literal);
                            lit_nx.insert(literal, nx);
                            nx
                        }
                    })
                }
            }
            literal_nodes
        };

    // while parsing:
    // remove the weighted edges and substitute it with the corresponding
    // structure that uses AND-Nodes and Literal-Nodes. Example:
    //
    //                   n1                       n1
    //                 /   \                   /    \
    //              Ln|    |Lm     into     AND    AND
    //                \   /                /   \  /   \
    //                 n2                 Ln    n2    Lm
    //
    //
    let resolve_weighted_edge = |ddnnf_graph: &mut StableGraph<TId, ()>,
                                 from: NodeIndex,
                                 to: NodeIndex,
                                 edge: EdgeIndex,
                                 weights: Vec<i32>| {
        let and_node = ddnnf_graph.add_node(TId::And);
        let literal_nodes = get_literal_indices(ddnnf_graph, weights);

        ddnnf_graph.remove_edge(edge);

        ddnnf_graph.add_edge(from, and_node, ());
        for node in literal_nodes {
            ddnnf_graph.add_edge(and_node, node, ());
        }
        ddnnf_graph.add_edge(and_node, to, ());
    };

    // opens the file with a BufReader and
    // works off each line of the file data seperatly
    for line in lines {
        let next: D4Token = lex_line_d4(line.as_ref()).unwrap().1;

        use D4Token::*;
        match next {
            Edge { from, to, features } => {
                for f in &features {
                    literal_occurences.borrow_mut()[f.unsigned_abs() as usize] = true;
                    total_features = max(total_features, f.unsigned_abs());
                }
                let from_n = indices[from as usize - 1];
                let to_n = indices[to as usize - 1];
                let edge = ddnnf_graph.add_edge(from_n, to_n, ());
                resolve_weighted_edge(&mut ddnnf_graph, from_n, to_n, edge, features);
            }
            And => indices.push(ddnnf_graph.add_node(TId::And)),
            Or => indices.push(ddnnf_graph.add_node(TId::Or)),
            True => indices.push(ddnnf_graph.add_node(TId::True)),
            False => indices.push(ddnnf_graph.add_node(TId::False)),
        }
    }

    let or_triangles: Rc<RefCell<Vec<Option<NodeIndex>>>> =                                         //or_triangles
        Rc::new(RefCell::new(vec![None; (total_features + 1) as usize]));

    let add_literal_node =
        |ddnnf_graph: &mut StableGraph<TId, ()>, f_u32: u32, attach: NodeIndex| {
            let f = f_u32 as i32;
            let mut ort = or_triangles.borrow_mut();

            if ort[f_u32 as usize].is_some() {
                ddnnf_graph.add_edge(attach, ort[f_u32 as usize].unwrap(), ());
            } else {
                let or = ddnnf_graph.add_node(TId::Or);
                ort[f_u32 as usize] = Some(or);

                let pos_lit = get_literal_indices(ddnnf_graph, vec![f])[0];
                let neg_lit = get_literal_indices(ddnnf_graph, vec![-f])[0];

                ddnnf_graph.add_edge(attach, or, ());
                ddnnf_graph.add_edge(or, pos_lit, ());
                ddnnf_graph.add_edge(or, neg_lit, ());
            }
        };

    let balance_or_children =
        |ddnnf_graph: &mut StableGraph<TId, ()>,
         from: NodeIndex,
         children: Vec<(NodeIndex, HashSet<u32>)>| {
            for child in children {
                let and_node = ddnnf_graph.add_node(TId::And);

                // place the newly created and node between the or node and its child
                ddnnf_graph.remove_edge(ddnnf_graph.find_edge(from, child.0).unwrap());
                ddnnf_graph.add_edge(from, and_node, ());
                ddnnf_graph.add_edge(and_node, child.0, ());

                for literal in child.1 {
                    add_literal_node(ddnnf_graph, literal, and_node);
                }
            }
        };

    // add a new root which hold the unmentioned variables within the total_features range
    let root = ddnnf_graph.add_node(TId::And);
    ddnnf_graph.add_edge(root, NodeIndex::new(0), ());

    // add literals that are not mentioned in the ddnnf to the new root node
    for i in 1..=total_features {
        if !literal_occurences.borrow()[i as usize] {
            add_literal_node(&mut ddnnf_graph, i, root);
        }
    }

    // Starting from an initial AND node, we delete all parent AND nodes.
    // We can do this because the start node has a FALSE node as children. Hence, it count is 0!
    let delete_parent_and_chain = |ddnnf_graph: &mut StableGraph<TId, ()>, start: NodeIndex| {
        let mut current_vec = Vec::new();
        let mut current = start;
        loop {
            if ddnnf_graph[current] == TId::And {
                // remove the AND node and all parent nodes that are also AND nodes
                let mut parents = ddnnf_graph.neighbors_directed(current, Incoming).detach();
                while let Some(parent) = parents.next_node(ddnnf_graph) {
                    current_vec.push(parent);
                }
                ddnnf_graph.remove_node(current);
            }

            match current_vec.pop() {
                Some(head) => current = head,
                None => break,
            }
        }
    };

    // second dfs:
    // Remove the True and False node if any is part of the dDNNF.
    // Those nodes can influence the core and dead features by reducing the amount of those,
    // we can identify simply by literal occurences.
    // Further, we decrease the size and complexity of the dDNNF.
    let mut dfs = DfsPostOrder::new(&ddnnf_graph, root);
    while let Some(nx) = dfs.next(&ddnnf_graph) {
        let mut neighbours = ddnnf_graph.neighbors_directed(nx, Outgoing).detach();
        loop {
            let next = neighbours.next(&ddnnf_graph);
            match next {
                Some((n_edge, n_node)) => {
                    if !ddnnf_graph.contains_node(n_node) {
                        continue;
                    }

                    if ddnnf_graph[n_node] == TId::True {
                        match ddnnf_graph[nx] {
                            TId::And => ddnnf_graph.remove_edge(n_edge).unwrap(),
                            TId::Or => (), // should never happen
                            _ => {
                                // Bold, Red, Foreground Color (see https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797)
                                eprintln!("\x1b[1;38;5;196mERROR: Unexpected Nodetype while encoutering a True node. Only OR and AND nodes can have children. Aborting...");
                                process::exit(1);
                            }
                        };
                    }

                    if ddnnf_graph[n_node] == TId::False {
                        match ddnnf_graph[nx] {
                            TId::Or => ddnnf_graph.remove_edge(n_edge).unwrap(),
                            TId::And => delete_parent_and_chain(&mut ddnnf_graph, nx),
                            _ => {
                                eprintln!("\x1b[1;38;5;196mERROR: Unexpected Nodetype while encoutering a False node. Only OR and AND nodes can have children. Aborting...");
                                process::exit(1);
                            }
                        };
                    }
                }
                None => break,
            }
        }
    }

    // third dfs:
    // Look at each or node. For each outgoing edge:
    // 1. Compute all literals that occur in the children of that edge
    // 2. Determine which literals occur only in the other paths
    // 3. Add those literals in the path we are currently looking at
    // Example:
    //
    //                                              OR
    //                  OR                       /      \
    //                /    \                   /         \
    //              Ln     AND      into     AND        AND
    //                    /   \             /   \      /   \
    //                   Lm   -Ln          Ln   OR    |   -Ln
    //                                         /  \  /
    //                                       -Lm   Lm
    //
    let mut safe: HashMap<NodeIndex, HashSet<u32>> = HashMap::new();
    let mut dfs = DfsPostOrder::new(&ddnnf_graph, root);
    while let Some(nx) = dfs.next(&ddnnf_graph) {
        // edges between going from an and node to another node do not
        // have any weights attached to them. Therefore, we can skip them
        if ddnnf_graph[nx] == TId::Or {
            let diffrences = get_literal_diff(&ddnnf_graph, &mut safe, &nx_literals.borrow(), nx);
            balance_or_children(&mut ddnnf_graph, nx, diffrences);
        }
    }

    // perform a depth first search to get the nodes ordered such
    // that child nodes are listed before their parents
    // transform that interim representation into a node vector
    dfs = DfsPostOrder::new(&ddnnf_graph, root);
    let mut nd_to_usize: HashMap<NodeIndex, usize> = HashMap::new();

    let mut parsed_nodes: Vec<Node> = Vec::with_capacity(ddnnf_graph.node_count());
    let mut literals: HashMap<i32, usize> = HashMap::new();
    let mut true_nodes = Vec::new();
    let nx_lit = nx_literals.borrow();

    while let Some(nx) = dfs.next(&ddnnf_graph) {
        nd_to_usize.insert(nx, parsed_nodes.len());
        let neighs = ddnnf_graph
            .neighbors(nx)
            .map(|n| *nd_to_usize.get(&n).unwrap())
            .collect::<Vec<usize>>();
        let next: Node = match ddnnf_graph[nx] {
            // extract the parsed Token
            TId::PositiveLiteral | TId::NegativeLiteral => {
                Node::new_literal(nx_lit.get(&nx).unwrap().to_owned())
            }
            TId::And => Node::new_and(calc_and_count(&mut parsed_nodes, &neighs), neighs),

            TId::Or => Node::new_or(0, calc_or_count(&mut parsed_nodes, &neighs), neighs),
            TId::True => Node::new_bool(true),
            TId::False => Node::new_bool(false),
            TId::Header => panic!("The d4 standard does not include a header!"),
        };

        match &next.ntype {
            NodeType::And { children } | NodeType::Or { children } => {
                let next_indize: usize = parsed_nodes.len();
                for &i in children {
                    parsed_nodes[i].parents.push(next_indize);
                }
            }
            // fill the FxHashMap with the literals
            NodeType::Literal { literal } => {
                literals.insert(*literal, parsed_nodes.len());
            }
            NodeType::True => {
                true_nodes.push(parsed_nodes.len());
            }
            _ => (),
        }

        parsed_nodes.push(next);
    }

    Ddnnf::new(parsed_nodes, literals, true_nodes, total_features, clauses)
}

// determine the differences in literal-nodes occuring in the child nodes
fn get_literal_diff(
    di_graph: &StableGraph<TId, ()>,
    safe: &mut HashMap<NodeIndex, HashSet<u32>>,
    nx_literals: &HashMap<NodeIndex, i32>,
    or_node: NodeIndex,
) -> Vec<(NodeIndex, HashSet<u32>)> {
    let mut inter_res = Vec::new();
    let neighbors = di_graph.neighbors_directed(or_node, Outgoing);

    for neighbor in neighbors {
        inter_res.push((
            neighbor,
            get_literals(di_graph, safe, nx_literals, neighbor),
        ));
    }

    let mut res: Vec<(NodeIndex, HashSet<u32>)> = Vec::new();
    for i in 0..inter_res.len() {
        let mut val: HashSet<u32> = HashSet::new();
        for (j, i_res) in inter_res.iter().enumerate() {
            if i != j {
                val.extend(&i_res.1);
            }
        }
        val = &val - &inter_res[i].1;
        if !val.is_empty() {
            res.push((inter_res[i].0, val));
        }
    }
    res
}

// determine what literal-nodes the current node is or which occur in its children
fn get_literals(
    di_graph: &StableGraph<TId, ()>,
    safe: &mut HashMap<NodeIndex, HashSet<u32>>,
    nx_literals: &HashMap<NodeIndex, i32>,
    or_child: NodeIndex,
) -> HashSet<u32> {
    let lookup = safe.get(&or_child);
    if let Some(x) = lookup {
        return x.clone();
    }

    let mut res = HashSet::new();
    use c2d_lexer::TokenIdentifier::*;
    match di_graph[or_child] {
        And | Or => {
            di_graph
                .neighbors_directed(or_child, Outgoing)
                .for_each(|n| res.extend(get_literals(di_graph, safe, nx_literals, n)));
            safe.insert(or_child, res.clone());
        }
        PositiveLiteral | NegativeLiteral => {
            res.insert(nx_literals.get(&or_child).unwrap().unsigned_abs());
            safe.insert(or_child, res.clone());
        }
        _ => (),
    }
    res
}

// multiplies the count of all child Nodes of an And Node
#[inline]
fn calc_and_count(nodes: &mut [Node], indices: &[usize]) -> Integer {
    Integer::product(indices.iter().map(|&index| &nodes[index].count)).complete()
}

// adds up the count of all child Nodes of an And Node
#[inline]
fn calc_or_count(nodes: &mut [Node], indices: &[usize]) -> Integer {
    Integer::sum(indices.iter().map(|&index| &nodes[index].count)).complete()
}

/// Is used to parse the queries in the config files
/// The format is:
/// -> A feature is either positiv or negative i32 value with a leading "-"
/// -> Multiple features in the same line form a query
/// -> Queries are seperated by a new line ("\n")
/// # Panic
///
/// Panics for a path to a non existing file
pub fn parse_queries_file(path: &str) -> Vec<(usize, Vec<i32>)> {
    let file = open_file_savely(path);

    let lines = BufReader::new(file)
        .lines()
        .map(|line| line.expect("Unable to read line"));
    let mut parsed_queries: Vec<(usize, Vec<i32>)> = Vec::new();

    for (line_number, line) in lines.enumerate() {
        // takes a line of the file and parses the i32 values
        let res: Vec<i32> = line
            .split_whitespace()
            .map(|elem| elem.parse::<i32>()
                .unwrap_or_else(|_| panic!("Unable to parse {:?} into an i32 value while trying to parse the querie file at {:?}.\nCheck the help page with \"-h\" or \"--help\" for further information.\n", elem, path)))
            .collect();
        parsed_queries.push((line_number, res));
    }
    parsed_queries
}

/// Tries to open a file.
/// If an error occurs the program prints the error and exists.
pub fn open_file_savely(path: &str) -> File {
    // opens the file with a BufReader and
    // works off each line of the file data seperatly
    match File::open(path) {
        Ok(x) => x,
        Err(err) => {
            // Bold, Red, Foreground Color (see https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797)
            eprintln!("\x1b[1;38;5;196mERROR: The following error code occured while trying to open the file \"{}\":\n{}\nAborting...", path, err);
            process::exit(1);
        }
    }
}
