require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class ReplicationTopologyTest < Verm::TestCase
  def setup
    # don't bother starting the standard verm instance
  end

  def port_for(instance_n)
    DEFAULT_VERM_SPAWNER_OPTIONS[:port] + instance_n
  end

  def post_something_to(verm)
    @location =
      post_file :path => '/foo',
                :file => 'binary_file.gz',
                :encoding => 'gzip',
                :expected_extension_suffix => 'gz',
                :type => 'application/octet-stream',
                :verm => verm
  end

  def get_options
    {
      :expected_content => File.read(fixture_file_path('binary_file.gz'), :mode => 'rb'),
      :expected_content_type => "application/octet-stream",
      :expected_content_encoding => 'gzip',
      :path => @location
    }
  end

  def test_propagates_around_open_loop
    0.upto(2) do |n|
      replicate_to = n.zero? ? nil : "localhost:#{port_for(n - 1)}"
      spawn_verm(:verm_data => "#{DEFAULT_VERM_SPAWNER_OPTIONS[:verm_data]}_replica#{n}", :port => port_for(n), :replicate_to => replicate_to)
    end

    before = spawners.collect {|spawner| get_statistics(:verm => spawner)}

    post_something_to(spawners[2])

    repeatedly_wait_until do
      get_statistics(:verm => spawners[1])[:replication_push_attempts] > 0
    end

    # all replicas should now have a copy
    spawners.each {|spawner| get get_options.merge(:verm => spawner)}

    # and all but the last should show successful pushes
    changes = spawners.collect.with_index {|spawner, index| calculate_statistics_change(before[index], get_statistics(:verm => spawner))}
    assert_equal([
      {:get_requests => 1, :put_requests => 2, :put_requests_missing_file_checks => 1, :put_requests_new_file_stored => 1},
      {:get_requests => 1, :put_requests => 1, :put_requests_new_file_stored => 1, :replication_push_attempts => 1},
      {:get_requests => 1, :post_requests => 1, :post_requests_new_file_stored => 1, :replication_push_attempts => 1},
    ], changes)
  end

  def test_propagates_around_closed_loop
    0.upto(2) do |n|
      replicate_to = n.zero? ? "localhost:#{port_for(2)}" : "localhost:#{port_for(n - 1)}"
      spawn_verm(:verm_data => "#{DEFAULT_VERM_SPAWNER_OPTIONS[:verm_data]}_replica#{n}", :port => port_for(n), :replicate_to => replicate_to)
    end

    before = spawners.collect {|spawner| get_statistics(:verm => spawner)}

    post_something_to(spawners[2])

    repeatedly_wait_until do
      get_statistics(:verm => spawners[2])[:put_requests_missing_file_checks] > 0
    end

    # all replicas should now have a copy
    spawners.each {|spawner| get get_options.merge(:verm => spawner)}

    # and all should have pushed, resulting in a new file on all but the original target
    changes = spawners.collect.with_index {|spawner, index| calculate_statistics_change(before[index], get_statistics(:verm => spawner))}
    assert_equal([
      {:get_requests => 1, :put_requests => 2, :put_requests_missing_file_checks => 1, :put_requests_new_file_stored => 1},
      {:get_requests => 1, :put_requests => 1, :put_requests_new_file_stored => 1, :replication_push_attempts => 1},
      {:get_requests => 1, :post_requests => 1, :post_requests_new_file_stored => 1, :replication_push_attempts => 1, :put_requests => 1, :put_requests_missing_file_checks => 1},
    ], changes)
  end

  def test_propagates_over_redundant_mesh
    0.upto(2) do |n|
      replicate_to = (0..2).collect {|rn| "localhost:#{port_for(rn)}" unless rn == n}.compact
      spawn_verm(:verm_data => "#{DEFAULT_VERM_SPAWNER_OPTIONS[:verm_data]}_replica#{n}", :port => port_for(n), :replicate_to => replicate_to)
    end

    before = spawners.collect {|spawner| get_statistics(:verm => spawner)}

    post_something_to(spawners[2])

    repeatedly_wait_until do
      get_statistics(:verm => spawners[2])[:put_requests_missing_file_checks] > 0
    end

    # all replicas should now have a copy
    spawners.each {|spawner| get get_options.merge(:verm => spawner)}

    # and all should have pushed, resulting in a new file on all but the original target
    changes = spawners.collect.with_index {|spawner, index| calculate_statistics_change(before[index], get_statistics(:verm => spawner))}
    assert_equal([
      {:get_requests => 1, :put_requests => 2, :put_requests_new_file_stored => 1, :put_requests_missing_file_checks => 1},
      {:get_requests => 1, :put_requests => 2, :put_requests_new_file_stored => 1, :put_requests_missing_file_checks => 1},
      {:get_requests => 1, :post_requests => 1, :post_requests_new_file_stored => 1, :put_requests => 2, :put_requests_missing_file_checks => 2, :replication_push_attempts => 2},
    ], changes)
  end
end
