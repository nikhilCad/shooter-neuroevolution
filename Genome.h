#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

// A NEAT genome: a variable-topology, strictly feed-forward directed graph.
// Node ids 0..inputCount-1 are the real inputs, inputCount is the always-on
// bias node, the next outputCount ids are outputs, and anything beyond that
// is a hidden node introduced later by an add-node mutation. Structure only
// ever grows from a minimal input-output-only starting genome — there's no
// fixed hidden layer size the way the old dense network had one.
enum class NodeType
{
    Input,
    Bias,
    Output,
    Hidden
};

struct NodeGene
{
    int id;
    NodeType type;
};

struct ConnectionGene
{
    int inNode;
    int outNode;
    float weight;
    bool enabled;
    int innovation;
};

struct Genome
{
    int inputCount = 0;
    int outputCount = 0;
    std::vector<NodeGene> nodes;
    std::vector<ConnectionGene> connections;
};

struct NodeSplitRecord
{
    int newNodeId;
    int inInnovation;
    int outInnovation;
};

// Historical markings so identical structural mutations arising independently
// (different genomes, possibly at different times) are assigned the same
// innovation numbers / node id, which is what lets Crossover align two
// genomes' genes meaningfully instead of comparing unrelated structure.
// Persists for the whole run as part of Evolution's state. Only the two
// counters are saved across a resumed run, not the lookup tables — after a
// resume, a mutation that happens to coincide with a pre-save one gets a
// fresh number instead of reusing the old one, which costs a little
// crossover-alignment quality in that edge case but never causes an id
// collision, since the counters alone already guarantee uniqueness.
struct InnovationTracker
{
    int nextNodeId = 0;
    int nextInnovationNumber = 0;
    std::unordered_map<uint64_t, int> connectionInnovations;       // (inNode,outNode) -> innovation
    std::unordered_map<int, NodeSplitRecord> nodeSplitByConnection; // split connection's innovation -> record
};

// A fresh genome with no hidden nodes: every input (plus the bias node)
// directly connected to every output, small random weights. This is NEAT's
// canonical minimal starting topology — all structure grows from here.
Genome CreateMinimalGenome(int inputCount, int outputCount, InnovationTracker &tracker);

// Runs the genome as a feed-forward graph and returns the outputCount output
// values, left un-activated (linear) — same convention the old dense network
// used: the caller applies tanh/sigmoid depending on what each output means.
std::vector<float> Activate(const Genome &genome, const std::vector<float> &inputs);

// --- Mutation ---

void MutateWeights(Genome &genome, float mutationRate, float mutationStrength);

// Adds a new connection between two previously-unconnected nodes, rejecting
// candidates that would create a cycle (this genome must stay feed-forward)
// or duplicate an existing gene. A no-op if no valid pair is found.
void MutateAddConnection(Genome &genome, InnovationTracker &tracker);

// Splits a random existing connection with a new hidden node: disables the
// old connection, wires the new node in with an incoming weight of 1 and an
// outgoing weight of 0. Starting the outgoing weight at 0 means the new node
// has no effect on behavior the instant it appears — only later weight
// mutations let it start actually contributing. (Canonical NEAT instead
// carries the old connection's weight forward on the new outgoing edge, which
// preserves the network's output exactly at the moment of the split; this is
// a simpler, more literal reading of "new nodes start at zero" at the cost of
// losing that old connection's contribution immediately rather than
// preserving it.) A no-op if the genome has no enabled connection to split.
void MutateAddNode(Genome &genome, InnovationTracker &tracker);

// Aligns connection genes by innovation number: matching genes are inherited
// from a random parent, disjoint/excess genes from the fitter parent only
// (ties favor a). Node genes are the union of whatever the inherited
// connections reference, plus every input/bias/output node.
Genome Crossover(const Genome &a, float fitnessA, const Genome &b, float fitnessB);

// NEAT's compatibility distance between two genomes, used to group the
// population into species: c1*excess/N + c2*disjoint/N + c3*avgWeightDiff,
// where N normalizes for genome size (1 for small genomes).
float GeneticDistance(const Genome &a, const Genome &b);
