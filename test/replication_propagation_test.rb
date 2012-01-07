require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class ReplicationPropagationTest < Verm::TestCase
  TIMEOUT_IN_DECISECONDS = 50

  def setup
    super
    REPLICATION_MASTER_VERM_SPAWNER.clear_data
    REPLICATION_MASTER_VERM_SPAWNER.start_verm
    REPLICATION_MASTER_VERM_SPAWNER.wait_until_available
  end

  def teardown
    REPLICATION_MASTER_VERM_SPAWNER.stop_verm
    super
  end

  def repeatedly_wait_until
    TIMEOUT_IN_DECISECONDS.times do
      return if yield
      sleep 0.1
    end
    raise TimeoutError
  end

  def assert_propagates_file(get_options)
    assert_statistics_change(:put_requests => 1, :get_requests => 1) do # on the slave
      before = get_statistics(:verm => REPLICATION_MASTER_VERM_SPAWNER)
  
      location = yield

      # wait until replication has been attempted
      changes = nil
      repeatedly_wait_until do
        after = get_statistics(:verm => REPLICATION_MASTER_VERM_SPAWNER)
        changes = calculate_statistics_change(before, after)
        changes[:replication_push_attempts]
      end

      assert_equal({:post_requests => 1, :post_requests_new_file_stored => 1, :replication_push_attempts => 1},
                   changes)

      # check the slave now has it
      get get_options.merge(:path => location)
    end
  end

  def test_propagates_new_files_to_slave
    assert_propagates_file(:expected_content => File.read(fixture_file_path('simple_text_file'))) do
      post_file :path => '/foo',
                :file => 'simple_text_file',
                :type => 'text/plain',
                :verm => REPLICATION_MASTER_VERM_SPAWNER
    end

    assert_propagates_file(:expected_content => File.read(fixture_file_path('binary_file'))) do
      post_file :path => '/foo',
                :file => 'binary_file',
                :type => 'application/octet-stream',
                :verm => REPLICATION_MASTER_VERM_SPAWNER
    end

    assert_propagates_file(:expected_content => File.read(fixture_file_path('binary_file.gz')), :expected_encoding => 'gzip') do
      post_file :path => '/foo',
                :file => 'binary_file.gz',
                :encoding => 'gzip',
                :type => 'application/octet-stream',
                :verm => REPLICATION_MASTER_VERM_SPAWNER
    end

    assert_propagates_file(:expected_content => File.read(fixture_file_path('medium_file'))) do
      post_file :path => '/foo',
                :file => 'medium_file',
                :type => 'application/octet-stream',
                :verm => REPLICATION_MASTER_VERM_SPAWNER
    end
  end
end
