const assert = require('assert');
const {
  renderBinding,
  validateManifest,
} = require('../../tools/generate-binding.js');

function manifest() {
  return {
    package: 'sample',
    headers: ['sample.h'],
    functions: [
      {
        name: 'sample_length',
        word: 'length',
        returns: 'usize',
        args: ['cstr'],
      },
    ],
  };
}

const rendered = renderBinding(validateManifest(manifest()));
assert(rendered.includes('#define TOY_EXTENSION_IMPLEMENTATION'));
assert(rendered.includes('"sample"'));
assert(rendered.includes('{.name = "length", .callback = binding_word_0}'));
assert(rendered.includes('sample_length(argument_0)'));
assert(rendered.includes("memchr(string_data_0, '\\0', string_length_0)"));
assert(rendered.includes('*toy_extension_init('));
assert(rendered.includes('toy_extension_bind(abi_version, api)'));
assert(!rendered.includes('TOY_EXTENSION_ABI_VERSION'));
assert(!rendered.includes('\0'));

const badHeader = manifest();
badHeader.headers = ['sample.h"\n#error injected'];
assert.throws(() => validateManifest(badHeader), /safe include path/);

const voidArgument = manifest();
voidArgument.functions[0].args = ['void'];
assert.throws(() => validateManifest(voidArgument), /cannot be void/);

const duplicateWord = manifest();
duplicateWord.functions.push({
  name: 'sample_other',
  word: 'length',
  returns: 'void',
  args: [],
});
assert.throws(() => validateManifest(duplicateWord), /duplicate Toy word/);

const unknownField = manifest();
unknownField.functions[0].return = 'usize';
assert.throws(() => validateManifest(unknownField), /not supported/);

function resourceManifest() {
  return {
    package: 'sample',
    headers: ['sample.h'],
    resources: [
      {
        name: 'handle',
        cType: 'sample_handle *',
        destructor: 'sample_handle_destroy',
      },
    ],
    functions: [
      {
        name: 'sample_handle_new',
        word: 'make-handle',
        returns: {resource: 'handle'},
        args: ['i64'],
      },
      {
        name: 'sample_handle_value',
        word: 'handle-value',
        returns: 'i64',
        args: [{resource: 'handle'}],
      },
      {
        name: 'sample_handle_open',
        word: 'open-handle',
        returns: {status: 'i32', success: [0, 1]},
        args: ['i64', {outResource: 'handle'}],
      },
    ],
  };
}

const resourceRendered = renderBinding(validateManifest(resourceManifest()));
assert(resourceRendered.includes('"sample.handle"'));
assert(resourceRendered.includes('sample_handle_destroy(value)'));
assert(resourceRendered.includes('toy_get_resource(state, 0'));
assert(resourceRendered.includes('sample_handle_open(argument_0, &argument_1)'));
assert(resourceRendered.includes('result == (int32_t)0 || result == (int32_t)1'));
assert(resourceRendered.includes('binding_destroy_resource_0(argument_1, NULL)'));

const policyManifest = manifest();
policyManifest.functions[0] = {
  name: 'sample_policy',
  word: 'policy',
  returns: {
    bytes: {
      lengthFunction: 'sample_policy_length',
      lengthType: 'u32',
    },
  },
  args: [
    {bytes: {length: 'i32'}},
    {const: 'i32', value: -1},
    {null: true},
    {cConstant: 'SAMPLE_COPY'},
  ],
};
const policyRendered = renderBinding(validateManifest(policyManifest));
assert(policyRendered.includes(
  'sample_policy(bytes_data_0, bytes_length_0, (int32_t)-1, NULL, SAMPLE_COPY)'
));
assert(policyRendered.includes('sample_policy_length('));
assert(policyRendered.includes('toy_push_string(state,'));

const dependentManifest = resourceManifest();
dependentManifest.resources.push({
  name: 'child',
  cType: 'sample_child *',
  destructor: 'sample_child_destroy',
  dependsOn: 'handle',
});
dependentManifest.functions.push({
  name: 'sample_child_open',
  word: 'open-child',
  returns: {status: 'i32', success: [0]},
  args: [{resource: 'handle'}, {outResource: 'child'}],
});
const dependentRendered = renderBinding(validateManifest(dependentManifest));
assert(dependentRendered.includes('toy_value_retain(state, 0)'));
assert(dependentRendered.includes('toy_value_release((toy_value *)userdata)'));

const detailedStatus = resourceManifest();
detailedStatus.functions[2].returns.error = {
  function: 'sample_handle_error',
  resource: 'handle',
};
const detailedRendered = renderBinding(validateManifest(detailedStatus));
assert(detailedRendered.includes(
  'argument_1 ? sample_handle_error(argument_1) : NULL'
));

const mappedStatus = manifest();
mappedStatus.functions[0].returns = {
  status: 'i32',
  map: {'0': false, '1': true},
};
const mappedRendered = renderBinding(validateManifest(mappedStatus));
assert(mappedRendered.includes('toy_push_bool(state, false)'));
assert(mappedRendered.includes('toy_push_bool(state, true)'));

const duplicateResource = resourceManifest();
duplicateResource.resources.push({...duplicateResource.resources[0]});
assert.throws(
  () => validateManifest(duplicateResource),
  /duplicate resource/
);

const unsafeCType = resourceManifest();
unsafeCType.resources[0].cType = 'sample_handle *; int injected';
assert.throws(() => validateManifest(unsafeCType), /C pointer type/);

const unknownResource = resourceManifest();
unknownResource.functions[0].returns.resource = 'missing';
assert.throws(() => validateManifest(unknownResource), /unknown resource/);

const scalarWithOutput = resourceManifest();
scalarWithOutput.functions[2].returns = 'i32';
assert.throws(
  () => validateManifest(scalarWithOutput),
  /output resources require void or status returns/
);

const duplicateSuccess = resourceManifest();
duplicateSuccess.functions[2].returns.success = [0, 0];
assert.throws(() => validateManifest(duplicateSuccess), /duplicate code/);

const outOfRangeSuccess = resourceManifest();
outOfRangeSuccess.functions[2].returns = {status: 'u8', success: [256]};
assert.throws(() => validateManifest(outOfRangeSuccess), /outside u8's range/);

const tooManyOutputs = resourceManifest();
tooManyOutputs.functions[2].args.push({outResource: 'handle'});
assert.throws(() => validateManifest(tooManyOutputs), /at most one output/);

const invalidConstant = manifest();
invalidConstant.functions[0].args = [{const: 'u8', value: -1}];
assert.throws(() => validateManifest(invalidConstant), /outside u8's range/);

const invalidNull = manifest();
invalidNull.functions[0].args = [{null: false}];
assert.throws(() => validateManifest(invalidNull), /null must be true/);

const invalidBytes = manifest();
invalidBytes.functions[0].args = [{bytes: {length: 'f32'}}];
assert.throws(() => validateManifest(invalidBytes), /must be an integer type/);

const invalidMappedStatus = manifest();
invalidMappedStatus.functions[0].returns = {
  status: 'i32',
  success: [0],
  map: {'1': true},
};
assert.throws(
  () => validateManifest(invalidMappedStatus),
  /exactly one of success or map/
);

const missingDependency = dependentManifest;
missingDependency.functions.at(-1).args = [{outResource: 'child'}];
assert.throws(
  () => validateManifest(missingDependency),
  /must have exactly one handle resource input/
);
